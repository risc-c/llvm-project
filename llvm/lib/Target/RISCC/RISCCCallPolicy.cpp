//===-- RISCCCallPolicy.cpp - Private call conventions --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCCallPolicy.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {
static cl::opt<bool> EnablePrivateNarrowCalls(
    "riscc-private-narrow-calls", cl::Hidden, cl::init(true),
    cl::desc(
        "Avoid ABI-only extensions in size-optimized private RISC-C calls"));
static cl::opt<unsigned> PrivateArgumentRegisters(
    "riscc-private-arg-registers", cl::Hidden, cl::init(6),
    cl::desc(
        "Maximum argument registers in size-optimized private RISC-C calls"));

// Private argument and extension policies need a closed set of size-oriented
// direct calls. Reject musttail on either side: it requires exact ABI matching.
// Calls is usable only when this returns true.
bool collectPrivateCalls(Function &F, SmallVectorImpl<CallInst *> &Calls) {
  if (F.isDeclaration() || !F.hasLocalLinkage() || F.hasAddressTaken() ||
      F.use_empty() || F.isVarArg() || !F.hasOptSize() || F.hasOptNone() ||
      (F.getCallingConv() != CallingConv::C &&
       F.getCallingConv() != CallingConv::Fast) ||
      F.hasFnAttribute("interrupt") || F.hasFnAttribute(Attribute::Naked))
    return false;

  for (User *U : F.users()) {
    auto *CI = dyn_cast<CallInst>(U);
    if (!CI || CI->getCalledFunction() != &F || CI->isMustTailCall() ||
        CI->getFunctionType() != F.getFunctionType() ||
        CI->getCallingConv() != F.getCallingConv() ||
        !CI->getFunction()->hasOptSize() || CI->getFunction()->hasOptNone())
      return false;
    Calls.push_back(CI);
  }
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (auto *CI = dyn_cast<CallInst>(&I); CI && CI->isMustTailCall())
        return false;
  return true;
}

// The public ABI is unchanged. A closed set of size-oriented direct calls can
// use otherwise callee-saved GPRs for arguments, with those registers becoming
// call-clobbered at both ends. Recompute the contract after IR optimization;
// stale annotations must not survive exposure of a function's address.
bool selectArgumentRegisters(Module &M) {
  unsigned NativeBits = M.getDataLayout().getPointerSizeInBits();
  bool Changed = false;
  for (Function &F : M) {
    unsigned Count = 3;
    SmallVector<CallInst *, 8> Calls;
    bool Eligible =
        PrivateArgumentRegisters > 3 && collectPrivateCalls(F, Calls);
    if (Eligible) {
      unsigned Limit = std::min(unsigned(PrivateArgumentRegisters), 6u);
      if (F.getFnAttribute("target-cpu").getValueAsString() == "nano")
        Limit = std::min(Limit, 5u); // R6 holds Nano's return address.
      unsigned Slots = 0;
      for (Argument &Arg : F.args()) {
        Type *Ty = Arg.getType();
        if ((!Ty->isIntegerTy() && !Ty->isPointerTy()) || Arg.hasByValAttr() ||
            Arg.hasStructRetAttr() || Arg.hasNestAttr()) {
          Eligible = false;
          break;
        }
        unsigned Parts = Ty->isPointerTy()
                             ? 1
                             : divideCeil(Ty->getIntegerBitWidth(), NativeBits);
        if (Slots + Parts > Limit)
          break; // Arguments are never split between registers and stack.
        Slots += Parts;
      }
      if (Eligible && Slots > 3)
        Count = Slots;
    }
    StringRef Old =
        F.getFnAttribute("riscc-private-reg-args").getValueAsString();
    std::string New = Count > 3 ? std::to_string(Count) : "";
    if (Old != New) {
      F.removeFnAttr("riscc-private-reg-args");
      if (!New.empty())
        F.addFnAttr("riscc-private-reg-args", New);
      Changed = true;
    }
  }
  return Changed;
}

enum ExtensionDemand { LowBits = 0, Signed = 1, Unsigned = 2, Unknown = 3 };

unsigned extensionAttrs(AttributeList Attrs, unsigned Index) {
  return (Attrs.hasAttributeAtIndex(Index, Attribute::SExt) ? Signed
                                                            : LowBits) |
         (Attrs.hasAttributeAtIndex(Index, Attribute::ZExt) ? Unsigned
                                                            : LowBits);
}

// Transparent users can share a single return normalization. Stores and
// truncations ignore high bits; casts and call boundaries name the extension
// they need. Unknown computations conservatively keep the existing contract.
unsigned returnDemand(Value *V, SmallPtrSetImpl<Value *> &Seen) {
  if (!Seen.insert(V).second)
    return LowBits;
  if (Seen.size() > 64)
    return Unknown;
  unsigned Demand = LowBits;
  for (Use &U : V->uses()) {
    auto *I = dyn_cast<Instruction>(U.getUser());
    if (!I)
      return Unknown;
    if (isa<PHINode, SelectInst>(I))
      Demand |= returnDemand(I, Seen);
    else if (isa<SExtInst>(I))
      Demand |= Signed;
    else if (isa<ZExtInst>(I))
      Demand |= Unsigned;
    else if (auto *CI = dyn_cast<CallInst>(I)) {
      if (!CI->isArgOperand(&U))
        return Unknown;
      Demand |=
          extensionAttrs(CI->getAttributes(), AttributeList::FirstArgIndex +
                                                  CI->getArgOperandNo(&U));
    } else if (auto *RI = dyn_cast<ReturnInst>(I))
      Demand |= extensionAttrs(RI->getFunction()->getAttributes(),
                               AttributeList::ReturnIndex);
    else if (!isa<TruncInst>(I) &&
             !(isa<StoreInst>(I) && U.getOperandNo() == 0))
      return Unknown;
  }
  return Demand;
}

// Removing an argument extension must not merely move it into the callee.
// Follow low-bit computations to their consumers; comparisons, right shifts,
// wider uses and extended returns still benefit from the incoming contract.
bool onlyNeedsLowBits(Value *V, SmallPtrSetImpl<Value *> &Seen) {
  if (!Seen.insert(V).second)
    return true;
  if (Seen.size() > 64)
    return false;
  for (Use &U : V->uses()) {
    auto *I = dyn_cast<Instruction>(U.getUser());
    if (!I)
      return false;
    if (isa<TruncInst>(I) || (isa<StoreInst>(I) && U.getOperandNo() == 0))
      continue;
    if (auto *RI = dyn_cast<ReturnInst>(I)) {
      if (extensionAttrs(RI->getFunction()->getAttributes(),
                         AttributeList::ReturnIndex) != LowBits)
        return false;
      continue;
    }
    bool LowOperation = isa<PHINode, SelectInst>(I);
    if (auto *BO = dyn_cast<BinaryOperator>(I))
      LowOperation |=
          BO->getOpcode() == Instruction::Add ||
          BO->getOpcode() == Instruction::Sub ||
          BO->getOpcode() == Instruction::Mul ||
          BO->getOpcode() == Instruction::And ||
          BO->getOpcode() == Instruction::Or ||
          BO->getOpcode() == Instruction::Xor ||
          (BO->getOpcode() == Instruction::Shl && U.getOperandNo() == 0);
    if (!LowOperation || !onlyNeedsLowBits(I, Seen))
      return false;
  }
  return true;
}

// A narrow IR argument/result only carries its low bits. The public ABI also
// specifies its extension to a register, but a closed set of private direct
// calls can agree on the consumers' extension, or leave high bits unspecified.
// Update both ends together; legalization still extends at actual wide uses.
// Keep the policy size-only, including every caller, and leave musttail's
// stricter ABI matching and externally observable function types untouched.
bool relaxPrivateExtensions(Module &M) {
  if (!EnablePrivateNarrowCalls)
    return false;
  unsigned NativeBits = M.getDataLayout().getPointerSizeInBits();
  auto IsNarrow = [=](Type *Ty) {
    return (Ty->isIntegerTy(8) || Ty->isIntegerTy(16)) &&
           Ty->getIntegerBitWidth() < NativeBits;
  };
  bool Changed = false;
  for (Function &F : M) {
    SmallVector<CallInst *, 8> Calls;
    if (!collectPrivateCalls(F, Calls))
      continue;
    if (IsNarrow(F.getReturnType()) &&
        (F.hasRetAttribute(Attribute::SExt) ||
         F.hasRetAttribute(Attribute::ZExt)) &&
        !F.hasRetAttribute(Attribute::Range)) {
      unsigned Demand = LowBits;
      SmallPtrSet<Value *, 16> Seen;
      for (CallInst *CI : Calls)
        Demand |= returnDemand(CI, Seen);
      if (Demand != Unknown) {
        for (Attribute::AttrKind Kind : {Attribute::SExt, Attribute::ZExt}) {
          bool Needed = Demand == (Kind == Attribute::SExt ? Signed : Unsigned);
          Changed |= F.hasRetAttribute(Kind) != Needed;
          if (Needed)
            F.addRetAttr(Kind);
          else
            F.removeRetAttr(Kind);
          for (CallInst *CI : Calls) {
            Changed |= CI->hasRetAttr(Kind) != Needed;
            if (Needed)
              CI->addRetAttr(Kind);
            else
              CI->removeRetAttr(Kind);
          }
        }
      }
    }
    for (Argument &Arg : F.args()) {
      if (!IsNarrow(Arg.getType()))
        continue;
      SmallPtrSet<Value *, 16> Seen;
      if (!onlyNeedsLowBits(&Arg, Seen))
        continue;
      unsigned N = Arg.getArgNo();
      for (Attribute::AttrKind Kind : {Attribute::SExt, Attribute::ZExt}) {
        Changed |= F.hasParamAttribute(N, Kind);
        F.removeParamAttr(N, Kind);
        for (CallInst *CI : Calls) {
          Changed |= CI->paramHasAttr(N, Kind);
          CI->removeParamAttr(N, Kind);
        }
      }
    }
  }
  return Changed;
}

// Conservatively recognize register-only sibling calls before legalization.
// A tail hint alone does not prove that the ABI permits a sibling transfer.
bool isSiblingCall(const User *U) {
  const auto *CI = dyn_cast<CallInst>(U);
  if (!CI || !CI->isTailCall() || CI->getFunctionType()->isVarArg())
    return false;
  const Function &Caller = *CI->getFunction();
  if (Caller.isVarArg() || Caller.hasFnAttribute("interrupt") ||
      Caller.getFnAttribute("disable-tail-calls").getValueAsString() ==
          "true" ||
      CI->getCallingConv() != Caller.getCallingConv())
    return false;
  const auto *Ret = dyn_cast_or_null<ReturnInst>(CI->getNextNode());
  if (!Ret || (Ret->getReturnValue() && Ret->getReturnValue() != CI))
    return false;
  unsigned NativeBits = Caller.getDataLayout().getPointerSizeInBits();
  unsigned Slots = 0;
  for (unsigned I = 0; I != CI->arg_size(); ++I) {
    if (CI->paramHasAttr(I, Attribute::ByVal) ||
        CI->paramHasAttr(I, Attribute::StructRet) ||
        CI->paramHasAttr(I, Attribute::Nest))
      return false;
    Type *Ty = CI->getArgOperand(I)->getType();
    if (!Ty->isIntegerTy() && !Ty->isPointerTy())
      return false;
    Slots += Ty->isPointerTy()
                 ? 1
                 : divideCeil(Ty->getIntegerBitWidth(), NativeBits);
    if (Slots > 3)
      return false;
  }
  return true;
}

bool selectLinks(Module &M) {
  SmallPtrSet<Function *, 32> TailOnly;
  SmallPtrSet<Function *, 32> PrivateLink;
  SmallVector<Function *, 32> Worklist;
  for (Function &F : M) {
    if (F.isDeclaration() || !F.hasLocalLinkage() || F.hasAddressTaken() ||
        F.hasFnAttribute("interrupt") || F.hasFnAttribute(Attribute::Naked))
      continue;
    if (!F.use_empty() && llvm::all_of(F.users(), isSiblingCall))
      TailOnly.insert(&F);
    else {
      PrivateLink.insert(&F);
      Worklist.push_back(&F);
    }
  }

  // Ordinary private calls use S3, leaving a public caller's S7 untouched
  // when the callee preserves it. A local function reached exclusively by
  // sibling transfers can instead inherit S7 from public callers, removing
  // both link-copy instructions without consuming a GPR. Keep S3 throughout
  // any tail-only chain also reachable from an S3 entry. This fixed point
  // handles recursive tail-call components and is independent of IR order.
  while (!Worklist.empty()) {
    Function *Caller = Worklist.pop_back_val();
    for (BasicBlock &BB : *Caller)
      for (Instruction &I : BB)
        if (auto *CI = dyn_cast<CallInst>(&I))
          if (Function *Callee = CI->getCalledFunction();
              TailOnly.contains(Callee) && PrivateLink.insert(Callee).second)
            Worklist.push_back(Callee);
  }

  bool Changed = false;
  for (Function &F : M) {
    // Recompute after IR optimization/LTO, before per-function codegen can
    // change the call graph. Never trust a stale object-private annotation.
    StringRef Link = PrivateLink.contains(&F) ? "s3" : "s7";
    if (F.getFnAttribute("riscc-link-register").getValueAsString() != Link) {
      F.addFnAttr("riscc-link-register", Link);
      Changed = true;
    }
  }
  return Changed;
}

class RISCCCallPolicyLegacy : public ModulePass {
public:
  static char ID;
  RISCCCallPolicyLegacy() : ModulePass(ID) {}
  bool runOnModule(Module &M) override {
    bool Changed = relaxPrivateExtensions(M);
    Changed |= selectArgumentRegisters(M);
    return selectLinks(M) || Changed;
  }
};
} // namespace

char RISCCCallPolicyLegacy::ID = 0;
INITIALIZE_PASS(RISCCCallPolicyLegacy, "riscc-call-policy",
                "Select RISC-C private link conventions", false, false)
ModulePass *llvm::createRISCCCallPolicyLegacyPass() {
  return new RISCCCallPolicyLegacy();
}
PreservedAnalyses RISCCCallPolicyPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = relaxPrivateExtensions(M);
  Changed |= selectArgumentRegisters(M);
  Changed |= selectLinks(M);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
