//===-- RISCCSRegAllocator.cpp - Plan use of the S-register cache --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RISC-C S registers are a small software-managed cache. They cannot
// participate in ordinary register allocation because ALU instructions cannot
// use them directly. After register and stack-slot coloring, this pass ranks
// the remaining spill slots and the function's entry values, then assigns the
// cache registers preserved by every call greedily.
//
//===----------------------------------------------------------------------===//

#include "RISCCSRegAllocator.h"
#include "RISCCInstrInfo.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "riscc-sreg-allocator"

namespace {

enum class CandidateKind { Spill, CalleeSave, ReturnAddress };

struct Candidate {
  uint64_t Weight;
  CandidateKind Kind;
  int FrameIndex = -1;
  MCRegister Reg;
};

static bool referencesFrameIndex(const MachineInstr &MI, int FI) {
  return llvm::any_of(MI.operands(), [FI](const MachineOperand &MO) {
    return MO.isFI() && MO.getIndex() == FI;
  });
}

static bool isSimpleSpillAccess(const MachineInstr &MI, int FI) {
  return (MI.getOpcode() == RISCC::STW ||
          MI.getOpcode() == RISCC::LDW) &&
         MI.getNumOperands() >= 3 && MI.getOperand(1).isFI() &&
         MI.getOperand(1).getIndex() == FI && MI.getOperand(2).isImm() &&
         MI.getOperand(2).getImm() == 0;
}

static bool analyzeSpillSlot(const MachineFunction &MF, int FI,
                             const MachineBlockFrequencyInfo &MBFI,
                             uint64_t &Weight) {
  bool HasStore = false;
  bool HasLoad = false;
  Weight = 0;

  for (const MachineBasicBlock &MBB : MF) {
    uint64_t Frequency = MBFI.getBlockFreq(&MBB).getFrequency();
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || !referencesFrameIndex(MI, FI))
        continue;
      if (!isSimpleSpillAccess(MI, FI))
        return false;

      HasStore |= MI.getOpcode() == RISCC::STW;
      HasLoad |= MI.getOpcode() == RISCC::LDW;
      Weight = SaturatingAdd(Weight, Frequency);
    }
  }
  return HasStore && HasLoad;
}

static void moveSpillToSReg(MachineFunction &MF, int FI, MCRegister SReg,
                            const RISCCInstrInfo &TII) {
  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstr &MI = *I++;
      if (MI.getNumOperands() < 3 || !MI.getOperand(1).isFI() ||
          MI.getOperand(1).getIndex() != FI)
        continue;

      if (MI.getOpcode() == RISCC::STW)
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCC::MTS), SReg)
            .addReg(MI.getOperand(0).getReg(),
                    getKillRegState(MI.getOperand(0).isKill()));
      else {
        assert(MI.getOpcode() == RISCC::LDW &&
               "S-register candidate contains a non-spill reference");
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCC::MFS),
                MI.getOperand(0).getReg())
            .addReg(SReg);
      }
      MI.eraseFromParent();
    }
  }
  MF.getFrameInfo().RemoveStackObject(FI);
}

static bool takeRegister(SmallVectorImpl<MCRegister> &Free,
                         MCRegister Reg) {
  auto I = llvm::find(Free, Reg);
  if (I == Free.end())
    return false;
  Free.erase(I);
  return true;
}

static bool isPreservedByAllCalls(const MachineFunction &MF, MCRegister Reg,
                                  const TargetRegisterInfo &TRI) {
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (!MI.isCall())
        continue;

      bool HasRegMask =
          llvm::any_of(MI.operands(),
                       [](const MachineOperand &MO) { return MO.isRegMask(); });
      if (!HasRegMask || MI.modifiesRegister(Reg, &TRI))
        return false;
    }
  }
  return true;
}

static bool allocateSRegisters(MachineFunction &MF,
                               const MachineBlockFrequencyInfo &MBFI) {
  const RISCCSubtarget &STI = MF.getSubtarget<RISCCSubtarget>();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *Info = MF.getInfo<RISCCMachineFunctionInfo>();
  Info->clearSRegPlan();
  if (STI.isNano() || MFI.hasTailCall())
    return false;

  MachineRegisterInfo &MRI = MF.getRegInfo();
  MCRegister Link = Info->getReturnAddressReg().asMCReg();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
  SmallVector<Candidate, 8> Candidates;

  for (int FI = 0; FI != MFI.getObjectIndexEnd(); ++FI) {
    if (MFI.isDeadObjectIndex(FI) || !MFI.isSpillSlotObjectIndex(FI))
      continue;
    uint64_t Weight;
    if (analyzeSpillSlot(MF, FI, MBFI, Weight))
      Candidates.push_back({Weight, CandidateKind::Spill, FI, MCRegister()});
  }

  uint64_t EntryFrequency = MBFI.getEntryFreq().getFrequency();
  for (MCRegister Reg : {RISCC::R5, RISCC::R6})
    if (MRI.isPhysRegUsed(Reg))
      Candidates.push_back(
          {SaturatingMultiply(EntryFrequency, uint64_t(2)),
           CandidateKind::CalleeSave, -1, Reg});

  bool HasLink = Link && MRI.isPhysRegUsed(Link);
  bool IsLinkClobbered =
      HasLink && llvm::any_of(MF, [&](const MachineBasicBlock &MBB) {
        return llvm::any_of(MBB, [&](const MachineInstr &MI) {
          return MI.modifiesRegister(Link, &TRI);
        });
      });
  if (HasLink && !IsLinkClobbered)
    Candidates.push_back(
        {SaturatingMultiply(EntryFrequency, uint64_t(4)),
         CandidateKind::ReturnAddress, -1, Link});

  llvm::stable_sort(Candidates, [](const Candidate &A, const Candidate &B) {
    if (A.Weight != B.Weight)
      return A.Weight > B.Weight;
    return A.Kind < B.Kind;
  });

  SmallVector<MCRegister, 5> Free;
  for (MCRegister Reg :
       {RISCC::S3, RISCC::S4, RISCC::S5, RISCC::S6, RISCC::S7})
    if (isPreservedByAllCalls(MF, Reg, TRI) &&
        (!MRI.isPhysRegUsed(Reg) || (Reg == Link && !IsLinkClobbered)))
      Free.push_back(Reg);

  // Flexible candidates avoid the precolored link register until its value is
  // less valuable than all the other available cache entries.
  if (HasLink && !IsLinkClobbered)
    llvm::stable_sort(Free, [Link](MCRegister A, MCRegister B) {
      return (A == Link) < (B == Link);
    });

  bool KeptLink = !HasLink;
  bool Changed = false;
  SmallVector<std::pair<int, MCRegister>, 5> SpillAssignments;
  for (const Candidate &C : Candidates) {
    if (C.Kind == CandidateKind::ReturnAddress) {
      if (takeRegister(Free, Link))
        KeptLink = true;
      continue;
    }
    if (Free.empty())
      continue;

    MCRegister SReg = Free.front();
    Free.erase(Free.begin());
    if (C.Kind == CandidateKind::Spill)
      SpillAssignments.emplace_back(C.FrameIndex, SReg);
    else {
      Info->setCalleeSavedSReg(C.Reg, SReg);
      Changed = true;
    }
  }

  if (!KeptLink && Info->getLRSpillFI() < 0) {
    Info->setLRSpillFI(MFI.CreateStackObject(2, Align(2), false));
    Changed = true;
  }
  for (auto [FI, SReg] : SpillAssignments)
    moveSpillToSReg(MF, FI, SReg, *STI.getInstrInfo());
  return Changed || !SpillAssignments.empty();
}

class RISCCSRegAllocatorLegacy final : public MachineFunctionPass {
public:
  static char ID;
  RISCCSRegAllocatorLegacy() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "RISC-C S-register allocator";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  bool runOnMachineFunction(MachineFunction &MF) override {
    return allocateSRegisters(
        MF, getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI());
  }
};

} // namespace

char RISCCSRegAllocatorLegacy::ID = 0;

INITIALIZE_PASS_BEGIN(RISCCSRegAllocatorLegacy, DEBUG_TYPE,
                      "Plan use of the RISC-C S-register cache", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_END(RISCCSRegAllocatorLegacy, DEBUG_TYPE,
                    "Plan use of the RISC-C S-register cache", false, false)

FunctionPass *llvm::createRISCCSRegAllocatorLegacyPass() {
  return new RISCCSRegAllocatorLegacy();
}

PreservedAnalyses
RISCCSRegAllocatorPass::run(MachineFunction &MF,
                            MachineFunctionAnalysisManager &MFAM) {
  bool Changed =
      allocateSRegisters(MF, MFAM.getResult<MachineBlockFrequencyAnalysis>(MF));
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<MachineBlockFrequencyAnalysis>();
  return PA;
}
