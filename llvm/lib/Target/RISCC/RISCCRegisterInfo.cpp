//===-- RISCCRegisterInfo.cpp - RISCC Register Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCRegisterInfo.h"
#include "RISCCInstrInfo.h"
#include "RISCCSubtarget.h"
#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "RISCCGenRegisterInfo.inc"

RISCCRegisterInfo::RISCCRegisterInfo() : RISCCGenRegisterInfo(RISCC::S7) {}

static void reserveInstructionRegisters(const MachineInstr &MI,
                                        RegScavenger &RS) {
  // The scavenger state is positioned immediately after MI.  A killed source
  // is consequently reported as free, but an address-materialization
  // sequence inserted before MI must not overwrite it.
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (Reg && Reg.isPhysical())
      RS.setRegUsed(Reg);
  }
}

const MCPhysReg *
RISCCRegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  return CSR_RISCC_SaveList;
}

const uint32_t *RISCCRegisterInfo::getCallPreservedMask(
    const MachineFunction &, CallingConv::ID) const {
  return CSR_RISCC_CallPreserved_RegMask;
}

BitVector RISCCRegisterInfo::getReservedRegs(const MachineFunction &) const {
  BitVector R(getNumRegs());
  R.set(RISCC::R7);
  for (MCRegister Reg : {RISCC::S0, RISCC::S1, RISCC::S2, RISCC::S3,
                         RISCC::S4, RISCC::S5, RISCC::S6, RISCC::S7})
    R.set(Reg);
  return R;
}

const TargetRegisterClass *RISCCRegisterInfo::getPointerRegClass(
    unsigned) const {
  return &RISCC::GPRRegClass;
}

bool RISCCRegisterInfo::eliminateFrameIndex(
    MachineBasicBlock::iterator II, int SPAdj, unsigned FIOperandNum,
    RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  int FI = MI.getOperand(FIOperandNum).getIndex();
  int64_t Offset = MF.getFrameInfo().getObjectOffset(FI) +
                   MF.getFrameInfo().getStackSize() +
                   MI.getOperand(FIOperandNum + 1).getImm() + SPAdj;

  if (MI.getOpcode() == RISCC::FRAMEADDR) {
    Register Dst = MI.getOperand(0).getReg();
    const auto &TII = *MF.getSubtarget<RISCCSubtarget>().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();
    BuildMI(*MI.getParent(), II, DL, TII.get(RISCC::MOV), Dst)
        .addReg(RISCC::R7);
    if (Offset) {
      if (isInt<8>(Offset))
        BuildMI(*MI.getParent(), II, DL, TII.get(RISCC::ADDI), Dst)
            .addReg(Dst).addImm(Offset);
      else {
        reserveInstructionRegisters(MI, *RS);
        Register Scratch = RS->FindUnusedReg(&RISCC::GPRRegClass);
        if (!Scratch)
          Scratch = RS->scavengeRegisterBackwards(RISCC::GPRRegClass, II,
                                                   false, SPAdj);
        BuildMI(*MI.getParent(), II, DL, TII.get(RISCC::LI), Scratch)
            .addImm(Offset);
        BuildMI(*MI.getParent(), II, DL, TII.get(RISCC::ADD), Dst)
            .addReg(Dst).addReg(Scratch, RegState::Kill);
      }
    }
    MI.eraseFromParent();
    return false;
  }

  if (isInt<8>(Offset)) {
    MI.getOperand(FIOperandNum).ChangeToRegister(RISCC::R7, false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return false;
  }

  assert(RS && "register scavenging required for large frame offset");
  reserveInstructionRegisters(MI, *RS);
  Register Scratch = RS->FindUnusedReg(&RISCC::GPRRegClass);
  if (!Scratch)
    Scratch =
        RS->scavengeRegisterBackwards(RISCC::GPRRegClass, II, false, SPAdj);
  assert(Scratch && "unable to scavenge frame-address register");
  const auto &TII = *MF.getSubtarget<RISCCSubtarget>().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  BuildMI(*MI.getParent(), II, DL, TII.get(RISCC::LI), Scratch).addImm(Offset);
  BuildMI(*MI.getParent(), II, DL, TII.get(RISCC::ADD), Scratch)
      .addReg(RISCC::R7).addReg(Scratch, RegState::Kill);
  MI.getOperand(FIOperandNum).ChangeToRegister(Scratch, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
  return false;
}

Register RISCCRegisterInfo::getFrameRegister(const MachineFunction &) const {
  return RISCC::R7;
}
