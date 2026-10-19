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
#include "llvm/Analysis/SimplifyQuery.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;

// For an even-width unsigned integer, x*x fits exactly when the upper half
// of x is zero. Avoid computing a double-width product just to obtain its
// overflow bit; the native multiply already supplies the low result.
static bool simplifyUnsignedSquareOverflow(Function &F) {
  unsigned NativeBits = F.getDataLayout().getPointerSizeInBits();
  bool Changed = false;
  for (BasicBlock &BB : F)
    for (Instruction &I : make_early_inc_range(BB)) {
      auto *II = dyn_cast<IntrinsicInst>(&I);
      if (!II || II->getIntrinsicID() != Intrinsic::umul_with_overflow ||
          II->getArgOperand(0) != II->getArgOperand(1) ||
          !II->getArgOperand(0)->getType()->isIntegerTy(NativeBits))
        continue;
      IRBuilder<> Builder(II);
      // The product and overflow flag must observe the same value even if
      // the original operand can contain undef bits.
      Value *X = Builder.CreateFreeze(II->getArgOperand(0), "square.input");
      Value *Product = Builder.CreateMul(X, X, "square.product");
      Value *Overflow = Builder.CreateICmpUGT(
          X,
          ConstantInt::get(X->getType(),
                           APInt::getLowBitsSet(NativeBits, NativeBits / 2)),
          "square.overflow");
      Value *Result = Builder.CreateInsertValue(PoisonValue::get(II->getType()),
                                                Product, 0);
      Result = Builder.CreateInsertValue(Result, Overflow, 1);
      II->replaceAllUsesWith(Result);
      II->eraseFromParent();
      Changed = true;
    }
  return Changed;
}

// A narrow wrapping add followed by an unsigned comparison describes a
// circular interval. Test the nonwrapping interval (or its complement) at
// native width instead: (unsigned)(zext(x) - lo) < length. This avoids
// masking the sum back to its original width during legalization. The same
// canonicalization can also make a native-width interval's constants cheaper.
// Keep both constants cheap and do not duplicate an add shared with another
// user.
static bool widenNarrowRangeChecks(Function &F) {
  unsigned NativeBits = F.getDataLayout().getPointerSizeInBits();
  bool Changed = false;
  for (BasicBlock &BB : F)
    for (Instruction &I : make_early_inc_range(BB)) {
      auto *Cmp = dyn_cast<ICmpInst>(&I);
      if (!Cmp || !Cmp->isUnsigned())
        continue;
      auto *Add = dyn_cast<BinaryOperator>(Cmp->getOperand(0));
      auto *Bound = dyn_cast<ConstantInt>(Cmp->getOperand(1));
      if (!Add || Add->getOpcode() != Instruction::Add || !Bound ||
          !Add->hasOneUse())
        continue;
      auto *Bias = dyn_cast<ConstantInt>(Add->getOperand(1));
      unsigned Bits = Bound->getBitWidth();
      if (!Bias || (Bits != 8 && Bits != 16 && Bits != NativeBits) ||
          Bits > NativeBits)
        continue;
      ConstantRange Range = ConstantRange::makeExactICmpRegion(
                                Cmp->getPredicate(), Bound->getValue())
                                .subtract(Bias->getValue());
      if (Range.isEmptySet() || Range.isFullSet())
        continue;
      bool Invert = Range.isWrappedSet();
      if (Invert)
        Range = Range.inverse();
      APInt Lower = Range.getLower().zext(NativeBits);
      APInt Length = Range.getUnsignedMax().zext(NativeBits) - Lower + 1;
      if (!(-Lower).isSignedIntN(8) || !Length.isIntN(8))
        continue;
      IRBuilder<> Builder(Cmp);
      Type *NativeTy = Builder.getIntNTy(NativeBits);
      Value *Wide = Builder.CreateZExt(Add->getOperand(0), NativeTy);
      Value *Offset =
          Builder.CreateAdd(Wide, ConstantInt::get(NativeTy, -Lower));
      Value *Result =
          Builder.CreateICmp(Invert ? ICmpInst::ICMP_UGE : ICmpInst::ICMP_ULT,
                             Offset, ConstantInt::get(NativeTy, Length));
      Cmp->replaceAllUsesWith(Result);
      Cmp->eraseFromParent();
      RecursivelyDeleteTriviallyDeadInstructions(Add);
      Changed = true;
    }
  return Changed;
}

// Add/subtract, bitwise operations and selections preserve their low bits
// independently of the upper bits. Keep a closed recurrence at native
// width when its narrow result is observed only outside the loop. Otherwise
// legalization can normalize every update instead of just the loop result.
// With PreserveRange, every operation also preserves zero upper bits. This
// permits logical right shifts and narrow observations inside the recurrence:
// normalize the inputs once, rather than masking each trip around the loop.
static bool widenNarrowRecurrences(Function &F, const LoopInfo &LI,
                                   const DominatorTree &DT,
                                   bool PreserveRange = false) {
  unsigned NativeBits = F.getDataLayout().getPointerSizeInBits();
  auto CanWiden = [&](Instruction *I) {
    if (isa<PHINode, SelectInst>(I) || I->isBitwiseLogicOp())
      return true;
    if (I->getOpcode() == Instruction::LShr)
      return PreserveRange;
    unsigned Opcode = I->getOpcode();
    if (Opcode != Instruction::Shl && Opcode != Instruction::Add &&
        Opcode != Instruction::Sub && Opcode != Instruction::Mul)
      return false;
    // Low-bit recurrences may have unspecified upper bits in their count;
    // zero-preserving recurrences can also safely widen a variable count.
    if (!PreserveRange)
      return Opcode != Instruction::Shl || isa<ConstantInt>(I->getOperand(1));
    if (I->hasNoUnsignedWrap())
      return true;

    // No-wrap flags are one proof, not a required syntactic shape. Use the
    // common overflow/known-bit analysis for masked, bounded and otherwise
    // provably nonwrapping arithmetic as well. Query the original narrow
    // operands, at the original instruction, before constructing wider IR.
    SimplifyQuery SQ(F.getDataLayout(), &DT, nullptr, I,
                     /*UseInstrInfo=*/true, /*CanUseUndef=*/false);
    Value *LHS = I->getOperand(0), *RHS = I->getOperand(1);
    switch (Opcode) {
    case Instruction::Add:
      return computeOverflowForUnsignedAdd(LHS, RHS, SQ) ==
             OverflowResult::NeverOverflows;
    case Instruction::Sub:
      return computeOverflowForUnsignedSub(LHS, RHS, SQ) ==
             OverflowResult::NeverOverflows;
    case Instruction::Mul:
      return computeOverflowForUnsignedMul(LHS, RHS, SQ) ==
             OverflowResult::NeverOverflows;
    case Instruction::Shl: {
      APInt MaxShift = computeKnownBits(RHS, SQ).getMaxValue();
      unsigned LeadingZeros = computeKnownBits(LHS, SQ).countMinLeadingZeros();
      return MaxShift.ult(I->getType()->getIntegerBitWidth()) &&
             MaxShift.ule(LeadingZeros);
    }
    default:
      llvm_unreachable("unexpected recurrence arithmetic");
    }
  };
  SmallVector<PHINode *, 16> Roots;
  for (Loop *L : LI.getLoopsInPreorder())
    for (PHINode &Phi : L->getHeader()->phis())
      if ((Phi.getType()->isIntegerTy(8) || Phi.getType()->isIntegerTy(16)) &&
          Phi.getType()->getIntegerBitWidth() < NativeBits)
        Roots.push_back(&Phi);
  SmallPtrSet<Instruction *, 32> Removed;
  bool Changed = false;
  for (PHINode *Root : Roots) {
    if (Removed.contains(Root))
      continue;
    Loop *L = LI.getLoopFor(Root->getParent());
    Type *NarrowTy = Root->getType();
    SmallVector<Instruction *, 16> Nodes{Root};
    SmallPtrSet<Instruction *, 32> Seen;
    Seen.insert(Root);
    bool Eligible = true, HasArithmetic = false, HasExitUse = false;
    for (unsigned N = 0; N < Nodes.size() && Eligible; ++N) {
      Instruction *I = Nodes[N];
      HasArithmetic |= isa<BinaryOperator>(I) && L->contains(I);
      auto Collect = [&](Instruction *Next) {
        if (Next && Next->getType() == NarrowTy && CanWiden(Next) &&
            Seen.insert(Next).second)
          Nodes.push_back(Next);
      };
      for (Value *V : I->operand_values()) {
        auto *Operand = dyn_cast<Instruction>(V);
        // Do not insert extensions after a value-producing terminator.
        if (Operand && Operand->isTerminator())
          Eligible = false;
        Collect(Operand);
      }
      for (User *U : I->users()) {
        auto *Use = dyn_cast<Instruction>(U);
        bool NarrowObservation =
            PreserveRange && Use && isa<TruncInst, ZExtInst>(Use);
        if (!Use || (!CanWiden(Use) && !NarrowObservation && L->contains(Use)))
          Eligible = false;
        else if (CanWiden(Use) && Use->getType() == NarrowTy)
          Collect(Use);
        else
          HasExitUse = true;
      }
      if (Nodes.size() > 32)
        Eligible = false;
    }
    if (!Eligible || !HasArithmetic || !HasExitUse)
      continue;

    Type *WideTy = IntegerType::get(F.getContext(), NativeBits);
    Value *Placeholder = PoisonValue::get(WideTy);
    DenseMap<Value *, Value *> Wide;
    for (Instruction *I : Nodes) {
      Instruction *New;
      if (auto *Phi = dyn_cast<PHINode>(I))
        New = PHINode::Create(WideTy, Phi->getNumIncomingValues(),
                              I->getName() + ".wide", I->getIterator());
      else if (auto *Select = dyn_cast<SelectInst>(I))
        New =
            SelectInst::Create(Select->getCondition(), Placeholder, Placeholder,
                               I->getName() + ".wide", I->getIterator());
      else
        New = BinaryOperator::Create(cast<BinaryOperator>(I)->getOpcode(),
                                     Placeholder, Placeholder,
                                     I->getName() + ".wide", I->getIterator());
      // The original overflow flags describe the narrow operation, not this
      // recurrence whose upper bits are deliberately unspecified.
      New->setDebugLoc(I->getDebugLoc());
      Wide[I] = New;
    }
    auto Extend = [&](Value *V) -> Value * {
      if (auto *Existing = Wide.lookup(V))
        return Existing;
      // A bounded recurrence needs zero upper bits. Otherwise either extension
      // preserves the low bits; sign extension keeps negative ADDIs cheap.
      if (auto *C = dyn_cast<ConstantInt>(V))
        return Wide[V] = ConstantInt::get(
                   WideTy, PreserveRange ? C->getValue().zext(NativeBits)
                                         : C->getValue().sext(NativeBits));
      IRBuilder<> Builder(F.getContext());
      if (auto *Def = dyn_cast<Instruction>(V))
        Builder.SetInsertPoint(Def->getParent(), std::next(Def->getIterator()));
      else
        Builder.SetInsertPoint(&*F.getEntryBlock().getFirstInsertionPt());
      return Wide[V] = Builder.CreateZExt(V, WideTy);
    };
    for (Instruction *I : Nodes) {
      auto *New = cast<Instruction>(Wide.lookup(I));
      if (auto *Phi = dyn_cast<PHINode>(I)) {
        auto *NewPhi = cast<PHINode>(New);
        for (unsigned N = 0; N < Phi->getNumIncomingValues(); ++N)
          NewPhi->addIncoming(Extend(Phi->getIncomingValue(N)),
                              Phi->getIncomingBlock(N));
      } else if (auto *Select = dyn_cast<SelectInst>(I)) {
        New->setOperand(1, Extend(Select->getTrueValue()));
        New->setOperand(2, Extend(Select->getFalseValue()));
      } else {
        New->setOperand(0, Extend(I->getOperand(0)));
        New->setOperand(1, Extend(I->getOperand(1)));
      }
      for (Use &U : make_early_inc_range(I->uses()))
        if (!Seen.contains(cast<Instruction>(U.getUser()))) {
          IRBuilder<> Builder(cast<Instruction>(U.getUser()));
          U.set(Builder.CreateTrunc(New, NarrowTy));
        }
    }
    for (Instruction *I : Nodes) {
      Removed.insert(I);
      I->dropAllReferences();
    }
    for (Instruction *I : Nodes)
      I->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

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
    Slice.push_back(Store);
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
static bool shortenLoopPointerLifetimes(Function &F, const DominatorTree &DT,
                                        const LoopInfo &LI) {
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

// LSR can choose an induction pointer biased toward several uses after the
// loop. Subword accesses cannot fold that bias on RISC-C, so carry their actual
// address instead. Only rebase when all non-update uses inside the loop share
// the same constant offset; this removes work without adding a live pointer.
static bool rebaseLoopPointers(Function &F, const LoopInfo &LI) {
  const DataLayout &DL = F.getDataLayout();
  bool Changed = false;
  for (Loop *L : LI.getLoopsInPreorder()) {
    BasicBlock *Header = L->getHeader();
    BasicBlock *Preheader = L->getLoopPreheader();
    BasicBlock *Latch = L->getLoopLatch();
    if (!Preheader || !Latch)
      continue;
    for (PHINode &Phi : make_early_inc_range(Header->phis())) {
      if (!Phi.getType()->isPointerTy() || Phi.getNumIncomingValues() != 2)
        continue;
      auto *Step =
          dyn_cast<GetElementPtrInst>(Phi.getIncomingValueForBlock(Latch));
      unsigned Bits = DL.getIndexTypeSizeInBits(Phi.getType());
      APInt Stride(Bits, 0);
      if (!Step || Step->getPointerOperand() != &Phi ||
          !Step->accumulateConstantOffset(DL, Stride))
        continue;
      APInt Bias(Bits, 0);
      bool Eligible = true, HasAccess = false;
      for (User *U : Phi.users()) {
        auto *I = dyn_cast<Instruction>(U);
        if (!I || I == Step || !L->contains(I))
          continue;
        auto *GEP = dyn_cast<GetElementPtrInst>(I);
        APInt Offset(Bits, 0);
        if (!GEP || GEP->getPointerOperand() != &Phi ||
            !GEP->accumulateConstantOffset(DL, Offset) || Offset.isZero() ||
            !Offset.isSignedIntN(8) || (!Bias.isZero() && Offset != Bias)) {
          Eligible = false;
          break;
        }
        Bias = Offset;
        for (User *GU : GEP->users()) {
          Type *AccessTy = nullptr;
          if (auto *Load = dyn_cast<LoadInst>(GU))
            AccessTy = Load->getType();
          else if (auto *Store = dyn_cast<StoreInst>(GU))
            if (Store->getPointerOperand() == GEP)
              AccessTy = Store->getValueOperand()->getType();
          HasAccess |=
              AccessTy && AccessTy->isIntegerTy() &&
              AccessTy->getIntegerBitWidth() < DL.getPointerSizeInBits();
        }
      }
      // Other uses of the updated pointer must not need an extra adjustment
      // on the backedge. Exit uses are recovered separately below.
      for (User *U : Step->users())
        if (auto *I = dyn_cast<Instruction>(U))
          if (I != &Phi && L->contains(I))
            Eligible = false;
      // An exit PHI would need recovery on its incoming edge. If that edge
      // also has a backedge successor, the adjustment stays in the hot loop.
      for (Instruction *Old :
           {static_cast<Instruction *>(&Phi), static_cast<Instruction *>(Step)})
        for (Use &U : Old->uses())
          if (auto *ExitPhi = dyn_cast<PHINode>(U.getUser()))
            if (ExitPhi != &Phi && L->contains(ExitPhi->getIncomingBlock(U)))
              Eligible = false;
      if (!Eligible || !HasAccess)
        continue;

      IRBuilder<> Builder(Preheader->getTerminator());
      Type *ByteTy = Builder.getInt8Ty();
      Value *Initial =
          Builder.CreateGEP(ByteTy, Phi.getIncomingValueForBlock(Preheader),
                            Builder.getInt(Bias), "address.initial");
      auto *Address = PHINode::Create(Phi.getType(), 2, "address.rebased",
                                      Phi.getIterator());
      Builder.SetInsertPoint(Step);
      Value *Next = Builder.CreateGEP(ByteTy, Address, Builder.getInt(Stride),
                                      "address.next");
      Address->addIncoming(Initial, Preheader);
      Address->addIncoming(Next, Latch);

      auto ReplaceUses = [&](Instruction *Old, Value *New) {
        for (Use &U : make_early_inc_range(Old->uses())) {
          auto *I = cast<Instruction>(U.getUser());
          if (I == &Phi || I == Step)
            continue;
          if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
            APInt Offset(Bits, 0);
            if (GEP->getPointerOperand() == Old &&
                GEP->accumulateConstantOffset(DL, Offset)) {
              Builder.SetInsertPoint(GEP);
              Value *Rebased =
                  Builder.CreateGEP(ByteTy, New, Builder.getInt(Offset - Bias),
                                    "address.adjusted");
              GEP->replaceAllUsesWith(Rebased);
              GEP->eraseFromParent();
              continue;
            }
          }
          if (auto *ExitPhi = dyn_cast<PHINode>(I))
            Builder.SetInsertPoint(
                ExitPhi->getIncomingBlock(U)->getTerminator());
          else
            Builder.SetInsertPoint(I);
          U.set(Builder.CreateGEP(ByteTy, New, Builder.getInt(-Bias),
                                  "address.recovered"));
        }
      };
      // Do not carry the old inbounds/no-wrap assumptions to biased pointers.
      ReplaceUses(&Phi, Address);
      ReplaceUses(Step, Next);
      Phi.dropAllReferences();
      Step->eraseFromParent();
      Phi.eraseFromParent();
      Changed = true;
    }
  }
  return Changed;
}

// Carry an address which the previous iteration already calculated. Generic
// GVN deliberately avoids PRE of GEPs and loop backedges; on RISC-C, repeating
// the scale and add is expensive because indexed addressing has no scale.
static bool reuseLoopAddresses(const DominatorTree &DT, const LoopInfo &LI) {
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
  bool Changed = simplifyUnsignedSquareOverflow(F);
  Changed |= widenNarrowRangeChecks(F);
  Changed |= sinkPartiallyDeadStores(F);
  // Store sinking changes the CFG; the loop transforms only change values.
  DominatorTree DT(F);
  LoopInfo LI(DT);
  Changed |= widenNarrowRecurrences(F, LI, DT);
  Changed |= widenNarrowRecurrences(F, LI, DT, /*PreserveRange=*/true);
  Changed |= shortenLoopPointerLifetimes(F, DT, LI);
  Changed |= rebaseLoopPointers(F, LI);
  Changed |= reuseLoopAddresses(DT, LI);
  Changed |= splitConstantReturns(F);
  return Changed;
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
