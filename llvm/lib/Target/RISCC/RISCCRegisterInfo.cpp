//===-- RISCCRegisterInfo.cpp - RISCC Register Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCRegisterInfo.h"
#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "RISCC.h"
#include "RISCCInstrInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include <algorithm>

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "RISCCGenRegisterInfo.inc"

RISCCRegisterInfo::RISCCRegisterInfo(const RISCCSubtarget &STI)
    : RISCCGenRegisterInfo(STI.isNano() ? RISCC::R6 : RISCC::S7), STI(STI) {}

const TargetRegisterClass *
RISCCRegisterInfo::getCrossCopyRegClass(const TargetRegisterClass *RC) const {
  // S registers transfer through a GPR; there is no direct S-to-S move.
  if (RC == &RISCC::SREGRegClass)
    return &RISCC::GPRRegClass;
  if (RC == &RISCC::SREG32RegClass)
    return &RISCC::GPR32RegClass;
  return RC;
}

ArrayRef<MCPhysReg>
RISCCRegisterInfo::getIntraCallClobberedRegs(const MachineFunction *) const {
  // Long branches and literal-pool repairs can use these after IPRA runs.
  static const MCPhysReg Scratch[] = {RISCC::R0, RISCC::S0};
  return Scratch;
}

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

static void reserveNanoReturnRegister(MachineBasicBlock::iterator I,
                                      RegScavenger &RS) {
  for (auto E = I->getParent()->end(); I != E; ++I) {
    if (I->getOpcode() != RISCC::RET_NANO)
      continue;
    RS.setRegUsed(I->getOperand(0).getReg());
    return;
  }
}

static Register scavengeFrameAddressRegister(const MachineInstr &MI,
                                             MachineBasicBlock::iterator I,
                                             RegScavenger *RS,
                                             const RISCCSubtarget &STI,
                                             int SPAdj) {
  assert(RS && "register scavenging required for large frame offset");
  reserveInstructionRegisters(MI, *RS);
  if (STI.isNano())
    reserveNanoReturnRegister(I, *RS);
  Register Scratch = RS->FindUnusedReg(STI.getGPRClass());
  if (!Scratch)
    Scratch =
        RS->scavengeRegisterBackwards(*STI.getGPRClass(), I, false, SPAdj);
  assert(Scratch && "unable to scavenge frame-address register");
  return Scratch;
}

const MCPhysReg *
RISCCRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  unsigned Count =
      MF ? getRISCCArgumentRegisterCount(MF->getFunction(), STI) : 3;
  static const MCPhysReg Private4[] = {RISCC::R5, RISCC::R6, RISCC::S5,
                                       RISCC::S6, 0};
  static const MCPhysReg Private5[] = {RISCC::R6, RISCC::S5, RISCC::S6, 0};
  static const MCPhysReg Private6[] = {RISCC::S5, RISCC::S6, 0};
  static const MCPhysReg Nano4[] = {RISCC::R5, 0};
  static const MCPhysReg Nano5[] = {0};
  if (Count > 3) {
    if (STI.isNano())
      return Count == 4 ? Nano4 : Nano5;
    return Count == 4 ? Private4 : Count == 5 ? Private5 : Private6;
  }
  return STI.isNano() ? CSR_RISCC_Nano_SaveList : CSR_RISCC_SaveList;
}

const uint32_t *RISCCRegisterInfo::getCallPreservedMask(const MachineFunction &,
                                                        CallingConv::ID) const {
  // This query describes a callee's public convention, not the caller's
  // private attributes. LowerCall adjusts the mask for a known private callee.
  return STI.isNano() ? CSR_RISCC_Nano_CallPreserved_RegMask
                      : CSR_RISCC_CallPreserved_RegMask;
}

bool RISCCRegisterInfo::isCalleeSavedPhysReg(MCRegister Reg,
                                             const MachineFunction &MF) const {
  // Unlike a call-site mask, this query describes the current function.
  // Keep spill-weight hints and debug entry-value tracking consistent with
  // the registers its prologue and epilogue actually preserve.
  unsigned Count = getRISCCArgumentRegisterCount(MF.getFunction(), STI);
  if ((Reg == RISCC::R4 && Count >= 4) || (Reg == RISCC::R5 && Count >= 5) ||
      (Reg == RISCC::R6 && Count >= 6))
    return false;
  return TargetRegisterInfo::isCalleeSavedPhysReg(Reg, MF);
}

const uint32_t *RISCCRegisterInfo::getSRegPreservingCallMask() const {
  assert(!STI.isNano() && "Nano has no S-register cache");
  return CSR_RISCC_SRegPreservingCall_RegMask;
}

bool RISCCRegisterInfo::isNonallocatableRegisterCalleeSave(
    MCRegister Reg) const {
  // Shrink wrapping must see both possible incoming links. The actual save
  // is planned by RISCCSRegAllocator, not the ordinary callee-save list.
  return !STI.isNano() && (Reg == RISCC::S7 || Reg == RISCC::S3);
}

BitVector RISCCRegisterInfo::getReservedRegs(const MachineFunction &) const {
  BitVector R(getNumRegs());
  R.set(RISCC::R7);
  for (MCRegister Reg : {RISCC::S0, RISCC::S1, RISCC::S2, RISCC::S3, RISCC::S4,
                         RISCC::S5, RISCC::S6, RISCC::S7})
    R.set(Reg);
  return R;
}

const TargetRegisterClass *
RISCCRegisterInfo::getPointerRegClass(unsigned) const {
  return STI.getGPRClass();
}

static void materializeFrameAddress(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    const DebugLoc &DL,
                                    const RISCCSubtarget &STI, Register Dst,
                                    int64_t Offset) {
  const RISCCInstrInfo &TII = *STI.getInstrInfo();
  assert(Dst != RISCC::R7 &&
         "frame address must not overwrite the stack pointer");
  // A copy plus one immediate add is cheaper than a general constant. RC32
  // also benefits from two adds instead of introducing a four-byte literal.
  unsigned MaxAdds = STI.isRC32() ? 2 : 1;
  if (Offset < -128 * int64_t(MaxAdds) || Offset > 127 * int64_t(MaxAdds)) {
    // The destination is available until this instruction defines it. Use it
    // for the offset as well, avoiding a scavenged register and an SP copy.
    TII.materializeImmediate(MBB, I, DL, Dst, Offset);
    BuildMI(MBB, I, DL, TII.get(TII.getNativeOpcode(RISCC::ADD)), Dst)
        .addReg(RISCC::R7)
        .addReg(Dst, RegState::Kill);
    return;
  }
  BuildMI(MBB, I, DL, TII.get(TII.getNativeOpcode(RISCC::MOV)), Dst)
      .addReg(RISCC::R7);
  while (Offset) {
    int64_t Step = std::clamp<int64_t>(Offset, -128, 127);
    BuildMI(MBB, I, DL, TII.get(TII.getNativeOpcode(RISCC::ADDI)), Dst)
        .addReg(Dst)
        .addImm(Step);
    Offset -= Step;
  }
}

bool RISCCRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  int FI = MI.getOperand(FIOperandNum).getIndex();
  int64_t Offset = MF.getFrameInfo().getObjectOffset(FI) +
                   MF.getFrameInfo().getStackSize() +
                   MI.getOperand(FIOperandNum + 1).getImm() + SPAdj;
  const bool IsRC32 = STI.isRC32();

  if (MI.getOpcode() == RISCC::FRAMEADDR ||
      MI.getOpcode() == RISCC::FRAMEADDR32) {
    Register Dst = MI.getOperand(0).getReg();
    DebugLoc DL = MI.getDebugLoc();
    materializeFrameAddress(*MI.getParent(), II, DL, STI, Dst, Offset);
    MI.eraseFromParent();
    return false;
  }

  if (STI.isLegalWordOffset(Offset)) {
    MI.getOperand(FIOperandNum).ChangeToRegister(RISCC::R7, false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return false;
  }

  Register Scratch = scavengeFrameAddressRegister(MI, II, RS, STI, SPAdj);
  const auto &TII = *MF.getSubtarget<RISCCSubtarget>().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  if (IsRC32) {
    materializeFrameAddress(*MI.getParent(), II, DL, STI, Scratch, Offset);
  } else {
    TII.materializeImmediate(*MI.getParent(), II, DL, Scratch, Offset);
    BuildMI(*MI.getParent(), II, DL, TII.get(RISCC::ADD), Scratch)
        .addReg(RISCC::R7)
        .addReg(Scratch, RegState::Kill);
  }
  MI.getOperand(FIOperandNum).ChangeToRegister(Scratch, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
  return false;
}

Register RISCCRegisterInfo::getFrameRegister(const MachineFunction &) const {
  return RISCC::R7;
}
