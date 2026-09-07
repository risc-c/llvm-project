//===-- RISCCFrameLowering.cpp - RISCC Frame Lowering ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCFrameLowering.h"
#include "RISCCInstrInfo.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/CGPassBuilderOption.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>

using namespace llvm;

RISCCFrameLowering::RISCCFrameLowering(const RISCCSubtarget &STI)
    : TargetFrameLowering(StackGrowsDown, STI.getStackAlignment(), 0,
                          STI.getStackAlignment()),
      STI(STI) {}

static void adjustSP(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                     const DebugLoc &DL, const RISCCInstrInfo &TII,
                     int64_t Amount, MachineInstr::MIFlag Flag, bool IsRC32,
                     Register Scratch = RISCC::R0) {
  if (!Amount)
    return;
  if (IsRC32) {
    while (Amount) {
      int64_t Step = std::clamp<int64_t>(Amount, -128, 127);
      BuildMI(MBB, I, DL, TII.get(RISCC::ADDI32), RISCC::R7)
          .addReg(RISCC::R7)
          .addImm(Step)
          .setMIFlag(Flag);
      Amount -= Step;
    }
    return;
  }
  if (isInt<8>(Amount)) {
    BuildMI(MBB, I, DL, TII.get(RISCC::ADDI), RISCC::R7)
        .addReg(RISCC::R7)
        .addImm(Amount)
        .setMIFlag(Flag);
    return;
  }
  TII.materializeImmediate(MBB, I, DL, Scratch, std::abs(Amount), Flag);
  BuildMI(MBB, I, DL, TII.get(Amount < 0 ? RISCC::SUB : RISCC::ADD), RISCC::R7)
      .addReg(RISCC::R7)
      .addReg(Scratch)
      .setMIFlag(Flag);
}

void RISCCFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  auto I = MBB.begin();
  DebugLoc DL = I == MBB.end() ? DebugLoc() : I->getDebugLoc();
  const auto &TII = *STI.getInstrInfo();
  uint64_t Size = MF.getFrameInfo().getStackSize();
  if (!STI.isRC32() && Size > 0xffff)
    report_fatal_error("RISC-C stack frame exceeds the 16-bit address space");
  adjustSP(MBB, I, DL, TII, -int64_t(Size), MachineInstr::FrameSetup,
           STI.isRC32());

  const auto &FuncInfo = *MF.getInfo<RISCCMachineFunctionInfo>();
  int FI = FuncInfo.getLRSpillFI();
  if (!STI.isNano() && FI >= 0) {
    BuildMI(MBB, I, DL, TII.get(RISCC::MFS), RISCC::R0)
        .addReg(FuncInfo.getReturnAddressReg())
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, I, DL, TII.get(STI.isRC32() ? RISCC::ST32 : RISCC::ST))
        .addReg(RISCC::R0, RegState::Kill)
        .addFrameIndex(FI)
        .addImm(0)
        .setMIFlag(MachineInstr::FrameSetup);
  }
}

void RISCCFrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  auto I = MBB.getLastNonDebugInstr();
  DebugLoc DL = I == MBB.end() ? DebugLoc() : I->getDebugLoc();
  const auto &TII = *STI.getInstrInfo();
  const auto &FuncInfo = *MF.getInfo<RISCCMachineFunctionInfo>();
  int FI = FuncInfo.getLRSpillFI();
  if (!STI.isNano() && FI >= 0) {
    BuildMI(MBB, I, DL, TII.get(STI.isRC32() ? RISCC::LD32 : RISCC::LD),
            RISCC::R0)
        .addFrameIndex(FI)
        .addImm(0)
        .setMIFlag(MachineInstr::FrameDestroy);
    BuildMI(MBB, I, DL, TII.get(RISCC::MTS), FuncInfo.getReturnAddressReg())
        .addReg(RISCC::R0, RegState::Kill)
        .setMIFlag(MachineInstr::FrameDestroy);
  }
  // A Nano tail return may hold its target in r0 while a large stack
  // adjustment needs a temporary.
  Register Scratch = STI.isNano() && I != MBB.end() &&
                             I->getOpcode() == RISCC::RET_NANO &&
                             I->getOperand(0).getReg() == RISCC::R0
                         ? RISCC::R6
                         : RISCC::R0;
  adjustSP(MBB, I, DL, TII, MF.getFrameInfo().getStackSize(),
           MachineInstr::FrameDestroy, STI.isRC32(), Scratch);
}

bool RISCCFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  if (STI.isNano())
    return false;

  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *FuncInfo = MF.getInfo<RISCCMachineFunctionInfo>();

  for (CalleeSavedInfo &Info : CSI) {
    if (Info.getReg() == RISCC::R4 || Info.getReg() == RISCC::R5 ||
        Info.getReg() == RISCC::R6) {
      MCRegister SReg = FuncInfo->getCalleeSavedSReg(Info.getReg());
      if (SReg) {
        Info.setDstReg(SReg);
        continue;
      }
    }

    const TargetRegisterClass *RC = RISCC::SREGRegClass.contains(Info.getReg())
                                        ? STI.getSRegClass()
                                        : STI.getGPRClass();
    Align Alignment = std::min(TRI->getSpillAlign(*RC), getStackAlign());
    int FI = MFI.CreateStackObject(TRI->getSpillSize(*RC), Alignment, true);
    MFI.setIsCalleeSavedObjectIndex(FI, true);
    Info.setFrameIdx(FI);
  }
  return true;
}

bool RISCCFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  const auto *TII = STI.getInstrInfo();
  for (const CalleeSavedInfo &Info : reverse(CSI))
    restoreCalleeSavedRegister(MBB, I, Info, TII, TRI);

  // RETS has no explicit GPR operands. Keep the restored ABI state live until
  // the return so post-RA COPY expansion cannot discard the restores.
  for (auto Term = I, End = MBB.end(); Term != End; ++Term) {
    if (!Term->isReturn())
      continue;
    for (const CalleeSavedInfo &Info : CSI)
      Term->addOperand(MachineOperand::CreateReg(Info.getReg(), false, true));
  }
  return true;
}

MachineBasicBlock::iterator RISCCFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // The reserved call frame keeps SP fixed even when custom expansion puts
  // call setup and teardown in different blocks.
  return MBB.erase(I);
}

static bool needsFrameScavengerSlot(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // Incoming arguments may lie beyond the short displacement range too.
  return MFI.getNumFixedObjects() || MFI.estimateStackSize(MF) > 127;
}

void RISCCFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  if (MF.getFrameInfo().getMaxAlign() > getStackAlign())
    report_fatal_error("RISC-C does not support stack realignment");
  bool HasReturningCall = llvm::any_of(MF, [](const MachineBasicBlock &MBB) {
    return llvm::any_of(MBB, [](const MachineInstr &MI) {
      return MI.isCall() && !MI.isReturn();
    });
  });
  if (!STI.isNano() && HasReturningCall &&
      MF.getInfo<RISCCMachineFunctionInfo>()->getLRSpillFI() < 0) {
    int FI = MF.getFrameInfo().CreateStackObject(
        STI.getSlotSize(), STI.getStackAlignment(), false);
    MF.getInfo<RISCCMachineFunctionInfo>()->setLRSpillFI(FI);
  }
  // RC16 reserves 14 bytes for frame setup/teardown in this estimate.
  int64_t EstimatedSize = MF.estimateFunctionSizeInBytes() + 14;
  if (STI.isRC32()) {
    // A small frame needs at most an SP adjustment and two LR instructions
    // at entry and each return. Larger frames reserve a scavenger slot below.
    EstimatedSize = 6;
    const RISCCInstrInfo &TII = *STI.getInstrInfo();
    for (const MachineBasicBlock &MBB : MF) {
      // Pool insertion may change the padding before an aligned block.
      EstimatedSize += std::max(MBB.getAlignment(), Align(2)).value() - 2;
      for (const MachineInstr &MI : MBB) {
        EstimatedSize += TII.getInstSizeInBytes(MI);
        if (MI.isReturn())
          EstimatedSize += 6;
        // Each use may need its own word, two alignment bytes, and a skip.
        switch (MI.getOpcode()) {
        default:
          break;
        case RISCC::LDPC:
        case RISCC::LDPC_BRANCH:
        case RISCC::CALL32_LITERAL:
        case RISCC::TAIL32_LITERAL:
          EstimatedSize += 8;
          break;
        case RISCC::SEXT16_RC32:
          // Expanded after frame finalization into two literal loads.
          EstimatedSize += 16;
          break;
        }
      }
    }
  }
  // Below this bound every short branch fits without relaxation.
  bool NeedsBranchSpill =
      (!STI.hasLongJall() || STI.isRC32()) && EstimatedSize >= 254;
  // Section splitting happens after frame finalization. Even a small function
  // can then need an indirect transfer between sections.
  if (STI.isRC32()) {
    const TargetMachine &TM = MF.getTarget();
    NeedsBranchSpill |= TM.getBBSectionsType() != BasicBlockSection::None ||
                        TM.Options.EnableMachineFunctionSplitter ||
                        getCGPassBuilderOption().EnableMachineFunctionSplitter;
  }
  // Reserve a scavenger slot only for large frame offsets or long branches.
  if (RS && (needsFrameScavengerSlot(MF) || NeedsBranchSpill)) {
    int FI = MF.getFrameInfo().CreateSpillStackObject(STI.getSlotSize(),
                                                      STI.getStackAlignment());
    RS->addScavengingFrameIndex(FI);
    MF.getInfo<RISCCMachineFunctionInfo>()->setBranchRelaxationSpillFI(FI);
  }
}
