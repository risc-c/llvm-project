//===-- RISCCInstrInfo.h - RISCC Instruction Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCINSTRINFO_H
#define LLVM_LIB_TARGET_RISCC_RISCCINSTRINFO_H

#include "RISCCRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "RISCCGenInstrInfo.inc"

namespace llvm {
class RISCCSubtarget;
class RISCCInstrInfo final : public RISCCGenInstrInfo {
  RISCCRegisterInfo RI;
  const RISCCSubtarget &STI;
  void anchor();

public:
  explicit RISCCInstrInfo(const RISCCSubtarget &);
  const RISCCRegisterInfo &getRegisterInfo() const { return RI; }
  static bool isConditionalBranchOpcode(unsigned Opcode);
  static unsigned getOppositeBranchOpcode(unsigned Opcode);

  void copyPhysReg(MachineBasicBlock &, MachineBasicBlock::iterator,
                   const DebugLoc &, Register, Register, bool,
                   bool = false, bool = false) const override;
  void storeRegToStackSlot(MachineBasicBlock &, MachineBasicBlock::iterator,
                           Register, bool, int, const TargetRegisterClass *,
                           Register, MachineInstr::MIFlag) const override;
  void loadRegFromStackSlot(MachineBasicBlock &, MachineBasicBlock::iterator,
                            Register, int, const TargetRegisterClass *,
                            Register, unsigned,
                            MachineInstr::MIFlag) const override;
  bool expandPostRAPseudo(MachineInstr &) const override;
  unsigned getInstSizeInBytes(const MachineInstr &) const override;
  bool reverseBranchCondition(SmallVectorImpl<MachineOperand> &) const override;
  bool analyzeBranch(MachineBasicBlock &, MachineBasicBlock *&,
                     MachineBasicBlock *&, SmallVectorImpl<MachineOperand> &,
                     bool) const override;
  unsigned removeBranch(MachineBasicBlock &, int * = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &, MachineBasicBlock *,
                        MachineBasicBlock *, ArrayRef<MachineOperand>,
                        const DebugLoc &, int * = nullptr) const override;
  bool isBranchOffsetInRange(unsigned, int64_t) const override;
  MachineBasicBlock *getBranchDestBlock(const MachineInstr &) const override;
  void insertIndirectBranch(MachineBasicBlock &, MachineBasicBlock &,
                            MachineBasicBlock &, const DebugLoc &, int64_t,
                            RegScavenger *) const override;
};
}

#endif
