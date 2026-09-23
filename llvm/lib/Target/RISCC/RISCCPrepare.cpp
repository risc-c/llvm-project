//===-- RISCCPrepare.cpp - Prepare IR for RISC-C code generation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "RISCCPrepare.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;

// Prove a complete overwrite along a short, unconditional path. Stop at any
// other memory access: even a load from a global may alias the stored pointer.
static bool overwritesBeforeObservation(BasicBlock *BB, const StoreInst &Store) {
  SmallPtrSet<BasicBlock *, 8> Visited;
  while (BB && Visited.size() < 8 && Visited.insert(BB).second) {
    for (Instruction &I : *BB) {
      if (auto *Next = dyn_cast<StoreInst>(&I))
        return Next->isSimple() &&
               Next->getPointerOperand() == Store.getPointerOperand() &&
               Next->getValueOperand()->getType() ==
                   Store.getValueOperand()->getType();
      if (I.mayReadOrWriteMemory() ||
          (!I.isTerminator() &&
           !isGuaranteedToTransferExecutionToSuccessor(&I)))
        return false;
    }
    auto *Br = dyn_cast<UncondBrInst>(BB->getTerminator());
    BB = Br ? Br->getSuccessor(0) : nullptr;
  }
  return false;
}

// Only sink computations wholly owned by the store. In particular, a pure
// call may move to fewer paths, but a reading, throwing or convergent call may
// not. Shared operands stay where they are and retain their existing uses.
static void collectStoreComputation(Value *V, BasicBlock *BB,
                                   SmallVectorImpl<Instruction *> &Slice,
                                   unsigned Depth = 0) {
  auto *I = dyn_cast<Instruction>(V);
  if (Depth == 8 || Slice.size() > 8 || !I || I->getParent() != BB ||
      !I->hasOneUse() || isa<PHINode, AllocaInst>(I) ||
      I->mayReadOrWriteMemory() || I->mayHaveSideEffects() || I->isTerminator())
    return;
  if (auto *Call = dyn_cast<CallBase>(I))
    if (Call->isConvergent() || Call->cannotDuplicate() ||
        Call->hasOperandBundles())
      return;
  for (Value *Operand : I->operand_values())
    collectStoreComputation(Operand, BB, Slice, Depth + 1);
  Slice.push_back(I);
}

// Avoid computing a default value on arms which immediately replace it.
// Keep this bounded: duplicating a small value tree onto at most two edges
// is useful for a register-poor target; large-scale store PRE belongs in DSE.
static bool sinkPartiallyDeadStores(Function &F) {
  SmallVector<StoreInst *> Stores;
  for (BasicBlock &BB : F) {
    Instruction *Term = BB.getTerminator();
    if (!isa<SwitchInst, CondBrInst>(Term))
      continue;
    Instruction *Prev = Term->getPrevNode();
    while (Prev && Prev->isDebugOrPseudoInst())
      Prev = Prev->getPrevNode();
    auto *Store = dyn_cast_or_null<StoreInst>(Prev);
    if (Store && Store->isSimple())
      Stores.push_back(Store);
  }
  bool Changed = false;
  for (StoreInst *Store : Stores) {
    BasicBlock *BB = Store->getParent();
    SmallPtrSet<BasicBlock *, 8> Seen;
    SmallVector<BasicBlock *> Live;
    unsigned Dead = 0;
    bool Eligible = true;
    for (BasicBlock *Succ : successors(BB)) {
      // Unique edges make splitting and PHI updates straightforward.
      if (Succ == BB || Succ->isEHPad() || !Seen.insert(Succ).second) {
        Eligible = false;
        break;
      }
      if (overwritesBeforeObservation(Succ, *Store))
        ++Dead;
      else
        Live.push_back(Succ);
    }
    if (!Eligible || !Dead || Live.empty() || Live.size() > 2 ||
        Dead < Live.size() || (F.hasOptSize() && Live.size() > 1))
      continue;
    SmallVector<Instruction *> Slice;
    collectStoreComputation(Store->getValueOperand(), BB, Slice);
    // A lone store is unlikely to pay for splitting an edge. Avoid excessive
    // duplication even in speed builds.
    if (Slice.size() < 3 || Slice.size() > 8)
      continue;
    for (BasicBlock *Succ : Live) {
      BasicBlock *Edge =
          SplitEdge(BB, Succ, nullptr, nullptr, nullptr, "store.needed");
      ValueToValueMapTy Map;
      for (Instruction *I : Slice) {
        Instruction *Clone = I->clone();
        RemapInstruction(Clone, Map, RF_IgnoreMissingLocals);
        Clone->insertBefore(Edge->getTerminator()->getIterator());
        Map[I] = Clone;
      }
      Instruction *Clone = Store->clone();
      RemapInstruction(Clone, Map, RF_IgnoreMissingLocals);
      Clone->insertBefore(Edge->getTerminator()->getIterator());
    }
    Value *StoredValue = Store->getValueOperand();
    Store->eraseFromParent();
    RecursivelyDeleteTriviallyDeadInstructions(StoredValue);
    Changed = true;
  }
  return Changed;
}

// A loop may need p + C while an exit needs p. Keep only the adjusted pointer
// across calls in the loop, then recover p with one ADDI at the exit. This
// avoids preserving two registers for two forms of the same address.
static bool shortenLoopPointerLifetimes(Function &F) {
  DominatorTree DT(F);
  LoopInfo LI(DT);
  const DataLayout &DL = F.getDataLayout();
  bool Changed = false;
  for (Loop *L : LI.getLoopsInPreorder()) {
    BasicBlock *Preheader = L->getLoopPreheader();
    if (!Preheader)
      continue;
    bool HasCall = llvm::any_of(L->blocks(), [](BasicBlock *BB) {
      return llvm::any_of(*BB, [](Instruction &I) {
        return isa<CallInst>(I) && !isa<IntrinsicInst>(I);
      });
    });
    if (!HasCall)
      continue;
    for (Instruction &I : *Preheader) {
      auto *GEP = dyn_cast<GetElementPtrInst>(&I);
      if (!GEP || !GEP->getType()->isPointerTy())
        continue;
      APInt Offset(DL.getIndexTypeSizeInBits(GEP->getType()), 0);
      if (!GEP->accumulateConstantOffset(DL, Offset) || Offset.isZero() ||
          !(-Offset).isSignedIntN(8))
        continue;
      Value *Base = GEP->getPointerOperand();
      if (!isa<Argument, Instruction>(Base) || isa<AllocaInst>(Base))
        continue;
      if (!llvm::any_of(GEP->users(), [L](User *U) {
            auto *Use = dyn_cast<Instruction>(U);
            return Use && L->contains(Use);
          }))
        continue;
      BasicBlock *Exit = nullptr;
      SmallVector<Use *> Uses;
      bool Eligible = true;
      for (Use &U : Base->uses()) {
        auto *User = dyn_cast<Instruction>(U.getUser());
        if (!User || User == GEP)
          continue;
        if (L->contains(User)) {
          Eligible = false;
          break;
        }
        if (!DT.dominates(L->getHeader(), User->getParent()))
          continue;
        if (isa<PHINode>(User) || User->getParent()->isEHPad() ||
            (Exit && Exit != User->getParent())) {
          Eligible = false;
          break;
        }
        Exit = User->getParent();
        Uses.push_back(&U);
      }
      if (!Eligible || Uses.empty())
        continue;
      // Do not let an inbounds assumption on the adjusted pointer introduce
      // poison into a use of the original, possibly still valid pointer.
      GEP->setNoWrapFlags(GEPNoWrapFlags::none());
      auto *IndexTy = DL.getIndexType(GEP->getType());
      auto *Original = GetElementPtrInst::Create(
          Type::getInt8Ty(F.getContext()), GEP,
          {ConstantInt::get(IndexTy, -Offset)}, "address.recovered",
          Exit->getFirstInsertionPt());
      for (Use *U : Uses)
        U->set(Original);
      Changed = true;
    }
  }
  return Changed;
}

// Carry an address which the previous iteration already calculated. Generic
// GVN deliberately avoids PRE of GEPs and loop backedges; on RISC-C, repeating
// the scale and add is expensive because indexed addressing has no scale.
static bool reuseLoopAddresses(Function &F) {
  DominatorTree DT(F);
  LoopInfo LI(DT);
  bool Changed = false;
  for (Loop *L : LI.getLoopsInPreorder()) {
    BasicBlock *Header = L->getHeader();
    BasicBlock *Preheader = L->getLoopPreheader();
    BasicBlock *Latch = L->getLoopLatch();
    if (!Preheader || !Latch)
      continue;
    for (Instruction &I : make_early_inc_range(*Header)) {
      auto *GEP = dyn_cast<GetElementPtrInst>(&I);
      if (!GEP)
        continue;
      PHINode *Index = nullptr;
      unsigned IndexOp = 0;
      bool Eligible = true;
      for (unsigned Op = 0; Op < GEP->getNumOperands(); ++Op) {
        Value *V = GEP->getOperand(Op);
        if (L->isLoopInvariant(V))
          continue;
        auto *Phi = dyn_cast<PHINode>(V);
        if (Index || !Phi || Phi->getParent() != Header ||
            Phi->getNumIncomingValues() != 2) {
          Eligible = false;
          break;
        }
        Index = Phi;
        IndexOp = Op;
      }
      if (!Eligible || !Index)
        continue;
      Value *BackIndex = Index->getIncomingValueForBlock(Latch);
      if (!isa<Instruction>(BackIndex))
        continue;
      GetElementPtrInst *BackGEP = nullptr;
      for (User *U : BackIndex->users()) {
        auto *Other = dyn_cast<GetElementPtrInst>(U);
        if (!Other || Other == GEP || Other->getType() != GEP->getType() ||
            Other->getSourceElementType() != GEP->getSourceElementType() ||
            Other->getNumOperands() != GEP->getNumOperands() ||
            !DT.dominates(Other, Latch->getTerminator()))
          continue;
        bool Match = true;
        for (unsigned Op = 0; Op < GEP->getNumOperands(); ++Op)
          Match &= Other->getOperand(Op) ==
                   (Op == IndexOp ? BackIndex : GEP->getOperand(Op));
        if (Match) {
          BackGEP = Other;
          break;
        }
      }
      if (!BackGEP)
        continue;
      auto *Initial = cast<GetElementPtrInst>(GEP->clone());
      Initial->setOperand(IndexOp, Index->getIncomingValueForBlock(Preheader));
      Initial->insertBefore(Preheader->getTerminator()->getIterator());
      auto *Address = PHINode::Create(GEP->getType(), 2, "address.carried",
                                      Header->begin());
      Address->addIncoming(Initial, Preheader);
      Address->addIncoming(BackGEP, Latch);
      // Different inbounds/no-wrap proofs must not introduce poison on a
      // path where the original address calculation was defined.
      Initial->setNoWrapFlags(GEPNoWrapFlags::none());
      BackGEP->setNoWrapFlags(GEPNoWrapFlags::none());
      GEP->replaceAllUsesWith(Address);
      GEP->eraseFromParent();
      Changed = true;
    }
  }
  return Changed;
}

// A shared return PHI can keep an immediate live around an entire loop. Give
// constant returns their own block so selection places the immediate directly
// in the return register, leaving scarce working registers for loop values.
static bool splitConstantReturns(Function &F) {
  bool Changed = false;
  for (BasicBlock &BB : make_early_inc_range(F)) {
    auto *Ret = dyn_cast<ReturnInst>(BB.getTerminator());
    if (!Ret || BB.size() != 2 || BB.hasAddressTaken())
      continue;
    auto *Phi = dyn_cast_or_null<PHINode>(Ret->getReturnValue());
    if (!Phi || Phi->getParent() != &BB || !Phi->hasOneUse())
      continue;
    DenseMap<ConstantInt *, BasicBlock *> Exits;
    for (unsigned I = Phi->getNumIncomingValues(); I-- != 0;) {
      auto *C = dyn_cast<ConstantInt>(Phi->getIncomingValue(I));
      if (!C || !C->getValue().isIntN(8) ||
          C->getBitWidth() > F.getDataLayout().getPointerSizeInBits())
        continue;
      BasicBlock *Pred = Phi->getIncomingBlock(I);
      Instruction *Br = Pred->getTerminator();
      if ((!isa<CondBrInst>(Br) && !isa<UncondBrInst>(Br)) ||
          llvm::count(successors(Pred), &BB) != 1)
        continue;
      BasicBlock *&Exit = Exits[C];
      if (!Exit) {
        Exit = BasicBlock::Create(F.getContext(), "return.constant", &F);
        ReturnInst::Create(F.getContext(), C, Exit)
            ->setDebugLoc(Ret->getDebugLoc());
      }
      Br->replaceSuccessorWith(&BB, Exit);
      Phi->removeIncomingValue(I, false);
      Changed = true;
    }
    if (Phi->getNumIncomingValues() == 0)
      BB.eraseFromParent();
    else if (Phi->getNumIncomingValues() == 1) {
      Phi->replaceAllUsesWith(Phi->getIncomingValue(0));
      Phi->eraseFromParent();
    }
  }
  return Changed;
}

namespace {
static bool prepare(Function &F) {
  bool Changed = sinkPartiallyDeadStores(F);
  Changed |= shortenLoopPointerLifetimes(F);
  Changed |= reuseLoopAddresses(F);
  return splitConstantReturns(F) || Changed;
}

class RISCCPrepareLegacy : public FunctionPass {
public:
  static char ID;
  RISCCPrepareLegacy() : FunctionPass(ID) {}
  bool runOnFunction(Function &F) override {
    return !skipFunction(F) && prepare(F);
  }
};
} // namespace
char RISCCPrepareLegacy::ID = 0;
INITIALIZE_PASS(RISCCPrepareLegacy, "riscc-prepare",
                "RISC-C code generation preparation", false, false)
FunctionPass *llvm::createRISCCPrepareLegacyPass() {
  return new RISCCPrepareLegacy();
}
PreservedAnalyses RISCCPreparePass::run(Function &F,
                                        FunctionAnalysisManager &) {
  return prepare(F) ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
