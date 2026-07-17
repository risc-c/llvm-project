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

using namespace llvm;

RISCCFrameLowering::RISCCFrameLowering(const RISCCSubtarget &STI)
    : TargetFrameLowering(StackGrowsDown, Align(2), 0, Align(2)), STI(STI) {}

static void adjustSP(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                     const DebugLoc &DL, const RISCCInstrInfo &TII,
                     int64_t Amount, MachineInstr::MIFlag Flag,
                     Register Scratch = RISCC::R0) {
  if (!Amount)
    return;
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
      .addReg(Scratch, RegState::Kill)
      .setMIFlag(Flag);
}

void RISCCFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  auto I = MBB.begin();
  DebugLoc DL = I == MBB.end() ? DebugLoc() : I->getDebugLoc();
  const auto &TII = *STI.getInstrInfo();
  uint64_t Size = MF.getFrameInfo().getStackSize();
  if (Size > 0xffff)
    report_fatal_error("RISC-C stack frame exceeds the 16-bit address space");
  adjustSP(MBB, I, DL, TII, -int64_t(Size), MachineInstr::FrameSetup);

  int FI = MF.getInfo<RISCCMachineFunctionInfo>()->getLRSpillFI();
  if (!STI.isNano() && FI >= 0) {
    BuildMI(MBB, I, DL, TII.get(RISCC::MFS), RISCC::R0)
        .addReg(RISCC::S7)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, I, DL, TII.get(RISCC::STW))
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
  int FI = MF.getInfo<RISCCMachineFunctionInfo>()->getLRSpillFI();
  if (!STI.isNano() && FI >= 0) {
    BuildMI(MBB, I, DL, TII.get(RISCC::LDW), RISCC::R0)
        .addFrameIndex(FI)
        .addImm(0)
        .setMIFlag(MachineInstr::FrameDestroy);
    BuildMI(MBB, I, DL, TII.get(RISCC::MTS), RISCC::S7)
        .addReg(RISCC::R0, RegState::Kill)
        .setMIFlag(MachineInstr::FrameDestroy);
  }
  // A Nano return may hold its target in r0 while a large SP adjustment also
  // needs a temporary. Calls and tail calls use direct targets here.
  Register Scratch =
      STI.isNano() && I != MBB.end() && I->getOpcode() == RISCC::RET_NANO &&
              I->getOperand(0).getReg() == RISCC::R0
          ? RISCC::R6
          : RISCC::R0;
  adjustSP(MBB, I, DL, TII, MF.getFrameInfo().getStackSize(),
           MachineInstr::FrameDestroy, Scratch);
}

MachineBasicBlock::iterator RISCCFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // Outgoing arguments live in the function's reserved call frame.  Keeping
  // SP fixed is important because wide-operation custom expansion can put the
  // setup and destroy pseudos in different basic blocks.
  return MBB.erase(I);
}

static bool needsFrameScavengerSlot(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // Fixed incoming objects also carry a caller-frame offset. Keep their
  // handling conservative instead of duplicating frame-layout calculations.
  return MFI.getNumFixedObjects() || MFI.estimateStackSize(MF) > 127;
}

void RISCCFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  if (MF.getFrameInfo().getMaxCallFrameSize() > 126)
    report_fatal_error("RISC-C supports outgoing call frames of at most 126 "
                       "bytes");
  bool HasReturningCall = llvm::any_of(MF, [](const MachineBasicBlock &MBB) {
    return llvm::any_of(MBB, [](const MachineInstr &MI) {
      return MI.isCall() && !MI.isReturn();
    });
  });
  if (!STI.isNano() && HasReturningCall) {
    int FI = MF.getFrameInfo().CreateStackObject(2, Align(2), false);
    MF.getInfo<RISCCMachineFunctionInfo>()->setLRSpillFI(FI);
  }
  // Min's largest forward short-branch displacement is 254 bytes. Reserve 14
  // bytes for frame setup/teardown when deciding whether an indirect long
  // branch may need to spill its scavenged address register.
  constexpr int64_t MinBranchSpillThreshold = 254 - 14;
  bool NeedsBranchSpill =
      !STI.hasSys() &&
      MF.estimateFunctionSizeInBytes() >= MinBranchSpillThreshold;
  // Only large frame offsets and long branches need a scavenged GPR. Avoid
  // growing every ordinary frame by a word just to reserve an unused slot.
  if (RS && (needsFrameScavengerSlot(MF) || NeedsBranchSpill)) {
    int FI = MF.getFrameInfo().CreateSpillStackObject(2, Align(2));
    RS->addScavengingFrameIndex(FI);
    if (NeedsBranchSpill)
      MF.getInfo<RISCCMachineFunctionInfo>()
          ->setBranchRelaxationSpillFI(FI);
  }
}
