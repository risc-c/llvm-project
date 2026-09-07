//===-- RISCCMachineFunctionInfo.h - RISCC Function Info --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_RISCC_RISCCMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {
class RISCCMachineFunctionInfo final : public MachineFunctionInfo {
  int LRSpillFI = -1;
  int BranchRelaxationSpillFI = -1;
  int VarArgsFrameIndex = 0;
  Register ReturnAddressReg;
  MCRegister R4SaveReg;
  MCRegister R5SaveReg;
  MCRegister R6SaveReg;

public:
  RISCCMachineFunctionInfo() = default;
  explicit RISCCMachineFunctionInfo(const Function &,
                                    const TargetSubtargetInfo *);
  int getLRSpillFI() const { return LRSpillFI; }
  void setLRSpillFI(int FI) { LRSpillFI = FI; }
  int getBranchRelaxationSpillFI() const { return BranchRelaxationSpillFI; }
  void setBranchRelaxationSpillFI(int FI) { BranchRelaxationSpillFI = FI; }
  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }
  Register getReturnAddressReg() const { return ReturnAddressReg; }
  void setReturnAddressReg(Register Reg) { ReturnAddressReg = Reg; }
  void clearSRegPlan() {
    R4SaveReg = MCRegister();
    R5SaveReg = MCRegister();
    R6SaveReg = MCRegister();
  }
  void setCalleeSavedSReg(MCRegister GPR, MCRegister SReg);
  MCRegister getCalleeSavedSReg(MCRegister GPR) const;
  virtual void anchor();
  static MachineFunctionInfo *create(BumpPtrAllocator &, const Function &,
                                     const TargetSubtargetInfo *);
};
} // namespace llvm

#endif
