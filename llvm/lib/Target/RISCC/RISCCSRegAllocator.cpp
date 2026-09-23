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
// cache registers greedily. Spills with disjoint lifetimes can share a
// register; only calls crossed by a live spill need to preserve its register.
//
//===----------------------------------------------------------------------===//

#include "RISCCSRegAllocator.h"
#include "RISCCInstrInfo.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
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
  return (MI.getOpcode() == RISCC::ST || MI.getOpcode() == RISCC::LD ||
          MI.getOpcode() == RISCC::ST32 || MI.getOpcode() == RISCC::LD32) &&
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

      HasStore |= MI.getOpcode() == RISCC::ST || MI.getOpcode() == RISCC::ST32;
      HasLoad |= MI.getOpcode() == RISCC::LD || MI.getOpcode() == RISCC::LD32;
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
      if (MI.isDebugInstr() && referencesFrameIndex(MI, FI)) {
        // The slot is going away. Tracking the replacement register here
        // would also require updating the variable's live range.
        if (MI.isDebugValue()) {
          for (MachineOperand &MO : MI.debug_operands())
            if (MO.isFI() && MO.getIndex() == FI)
              MO.ChangeToRegister(Register(), false);
        } else
          MI.eraseFromParent();
        continue;
      }
      if (MI.getNumOperands() < 3 || !MI.getOperand(1).isFI() ||
          MI.getOperand(1).getIndex() != FI)
        continue;

      if (MI.getOpcode() == RISCC::ST || MI.getOpcode() == RISCC::ST32)
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCC::MTS), SReg)
            .addReg(MI.getOperand(0).getReg(),
                    getKillRegState(MI.getOperand(0).isKill()));
      else {
        assert((MI.getOpcode() == RISCC::LD || MI.getOpcode() == RISCC::LD32) &&
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

static bool takeRegister(SmallVectorImpl<MCRegister> &Free, MCRegister Reg) {
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
      // Tail calls run after the epilogue has restored our saved values.
      if (!MI.isCall() || MI.isReturn())
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

// Stack-slot coloring has already merged compatible slots. Recompute their
// liveness here so call clobbers and S-register reuse use the same information.
struct SpillLiveness {
  SmallVector<BitVector> LiveIn;
  SmallVector<BitVector> Conflicts;
  SmallVector<BitVector> Clobbers;

  SpillLiveness(const MachineFunction &MF, ArrayRef<Candidate> Candidates,
                const TargetRegisterInfo &TRI) {
    unsigned Slots = MF.getFrameInfo().getObjectIndexEnd();
    BitVector IsSpill(Slots);
    for (const Candidate &C : Candidates)
      if (C.Kind == CandidateKind::Spill)
        IsSpill.set(C.FrameIndex);
    if (IsSpill.none())
      return;
    LiveIn.assign(MF.getNumBlockIDs(), BitVector(Slots));
    SmallVector<BitVector> LiveOut(LiveIn.size(), BitVector(Slots));
    Conflicts.assign(Slots, BitVector(Slots));
    Clobbers.assign(Slots, BitVector(TRI.getNumRegs()));

    auto Slot = [&](const MachineInstr &MI) -> int {
      if (MI.isDebugInstr() || MI.getNumOperands() < 3 ||
          !MI.getOperand(1).isFI())
        return -1;
      int FI = MI.getOperand(1).getIndex();
      return FI >= 0 && IsSpill.test(FI) ? FI : -1;
    };

    bool Changed;
    do {
      Changed = false;
      for (const MachineBasicBlock &MBB : llvm::reverse(MF)) {
        BitVector Live(Slots);
        for (const MachineBasicBlock *Succ : MBB.successors())
          Live |= LiveIn[Succ->getNumber()];
        LiveOut[MBB.getNumber()] = Live;
        for (const MachineInstr &MI : llvm::reverse(MBB))
          if (int FI = Slot(MI); FI >= 0) {
            if (MI.mayStore())
              Live.reset(FI);
            else
              Live.set(FI);
          }
        if (Live != LiveIn[MBB.getNumber()]) {
          LiveIn[MBB.getNumber()] = std::move(Live);
          Changed = true;
        }
      }
    } while (Changed);

    for (const MachineBasicBlock &MBB : MF) {
      BitVector Live = LiveOut[MBB.getNumber()];
      for (const MachineInstr &MI : llvm::reverse(MBB)) {
        if (MI.isCall()) {
          bool HasMask = llvm::any_of(
              MI.operands(), [](const auto &MO) { return MO.isRegMask(); });
          for (MCRegister Reg : {RISCC::S2, RISCC::S3, RISCC::S4, RISCC::S5,
                                 RISCC::S6, RISCC::S7})
            if (!HasMask || MI.modifiesRegister(Reg, &TRI))
              for (unsigned FI : Live.set_bits())
                Clobbers[FI].set(Reg);
        }
        if (int FI = Slot(MI); FI >= 0) {
          if (MI.mayStore()) {
            // Even a dead store must not overwrite another live spill.
            Conflicts[FI] |= Live;
            for (unsigned Other : Live.set_bits())
              Conflicts[Other].set(FI);
            Live.reset(FI);
          } else
            Live.set(FI);
        }
      }
      for (unsigned FI : Live.set_bits())
        Conflicts[FI] |= Live;
    }
  }
};

static bool allocateSRegisters(MachineFunction &MF,
                               const MachineBlockFrequencyInfo &MBFI) {
  const RISCCSubtarget &STI = MF.getSubtarget<RISCCSubtarget>();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *Info = MF.getInfo<RISCCMachineFunctionInfo>();
  Info->clearSRegPlan();
  if (STI.isNano())
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
  for (MCRegister Reg : {RISCC::R4, RISCC::R5, RISCC::R6})
    if (MRI.isPhysRegUsed(Reg))
      Candidates.push_back({SaturatingMultiply(EntryFrequency, uint64_t(2)),
                            CandidateKind::CalleeSave, -1, Reg});

  // Calls define the link even when a function never returns. Only an actual
  // read (a return or forwarding tail call) needs the entry address preserved.
  bool HasLink = Link && !MRI.use_nodbg_empty(Link);
  bool IsLinkClobbered =
      HasLink && llvm::any_of(MF, [&](const MachineBasicBlock &MBB) {
        return llvm::any_of(MBB, [&](const MachineInstr &MI) {
          return !(MI.isCall() && MI.isReturn()) &&
                 MI.modifiesRegister(Link, &TRI);
        });
      });
  if (HasLink)
    Candidates.push_back({SaturatingMultiply(EntryFrequency, uint64_t(4)),
                          CandidateKind::ReturnAddress, -1, Link});

  llvm::stable_sort(Candidates, [](const Candidate &A, const Candidate &B) {
    if (A.Weight != B.Weight)
      return A.Weight > B.Weight;
    return A.Kind < B.Kind;
  });

  SpillLiveness Spills(MF, Candidates, TRI);
  SmallVector<MCRegister, 6> Free;
  for (MCRegister Reg :
       {RISCC::S2, RISCC::S3, RISCC::S4, RISCC::S5, RISCC::S6, RISCC::S7})
    // Explicit operands still reserve a register, including link moves before
    // tail calls. Register-mask clobbers are checked for each candidate below.
    if (!MRI.isPhysRegUsed(Reg, /*SkipRegMaskTest=*/true) ||
        (Reg == Link && !IsLinkClobbered))
      Free.push_back(Reg);

  // Flexible candidates avoid the precolored link register until its value is
  // less valuable than all the other available cache entries.
  if (HasLink && !IsLinkClobbered)
    llvm::stable_sort(Free, [Link](MCRegister A, MCRegister B) {
      return (A == Link) < (B == Link);
    });

  bool KeptLink = !HasLink;
  bool Changed = false;
  SmallVector<std::pair<int, MCRegister>, 6> SpillAssignments;
  auto UsedBySpill = [&](MCRegister Reg) {
    return llvm::any_of(SpillAssignments,
                        [Reg](const auto &A) { return A.second == Reg; });
  };
  for (const Candidate &C : Candidates) {
    if (C.Kind == CandidateKind::ReturnAddress && !IsLinkClobbered) {
      if (!UsedBySpill(Link) && takeRegister(Free, Link))
        KeptLink = true;
      continue;
    }
    if (C.Kind == CandidateKind::Spill) {
      auto It = llvm::find_if(Free, [&](MCRegister Reg) {
        return !Spills.Clobbers[C.FrameIndex].test(Reg) &&
               llvm::none_of(SpillAssignments, [&](const auto &A) {
                 return A.second == Reg &&
                        Spills.Conflicts[C.FrameIndex].test(A.first);
               });
      });
      if (It != Free.end())
        SpillAssignments.emplace_back(C.FrameIndex, *It);
      continue;
    }

    auto SRegIt = llvm::find_if(Free, [&](MCRegister Reg) {
      return (Reg == RISCC::S2 || Reg == RISCC::S3 || Reg == RISCC::S4 ||
              Reg == RISCC::S7) &&
             !UsedBySpill(Reg) && isPreservedByAllCalls(MF, Reg, TRI);
    });
    if (SRegIt == Free.end())
      continue;
    if (C.Kind == CandidateKind::ReturnAddress) {
      Info->setLRSaveReg(*SRegIt);
      KeptLink = true;
    } else
      Info->setCalleeSavedSReg(C.Reg, *SRegIt);
    Free.erase(SRegIt);
    Changed = true;
  }

  if (!KeptLink && Info->getLRSpillFI() < 0) {
    Info->setLRSpillFI(MFI.CreateStackObject(STI.getSlotSize(),
                                             STI.getStackAlignment(), false));
    Changed = true;
  }
  for (auto [FI, SReg] : SpillAssignments) {
    moveSpillToSReg(MF, FI, SReg, *STI.getInstrInfo());
    for (MachineBasicBlock &MBB : MF)
      if (Spills.LiveIn[MBB.getNumber()].test(FI) && !MBB.isLiveIn(SReg))
        MBB.addLiveIn(SReg);
  }
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
