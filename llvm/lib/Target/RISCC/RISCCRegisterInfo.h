//===-- RISCCRegisterInfo.h - RISCC Register Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCREGISTERINFO_H
#define LLVM_LIB_TARGET_RISCC_RISCCREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "RISCCGenRegisterInfo.inc"

namespace llvm {
class RISCCSubtarget;
class RISCCRegisterInfo final : public RISCCGenRegisterInfo {
  const RISCCSubtarget &STI;

public:
  explicit RISCCRegisterInfo(const RISCCSubtarget &);
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &,
                                       CallingConv::ID) const override;
  BitVector getReservedRegs(const MachineFunction &) const override;
  const TargetRegisterClass *getPointerRegClass(unsigned = 0) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator, int, unsigned,
                           RegScavenger *) const override;
  Register getFrameRegister(const MachineFunction &) const override;
  bool requiresRegisterScavenging(const MachineFunction &) const override {
    return true;
  }
};
}

#endif
