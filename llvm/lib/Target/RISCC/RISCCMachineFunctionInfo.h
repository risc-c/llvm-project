//===-- RISCCMachineFunctionInfo.h - RISCC Function Info --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_RISCC_RISCCMACHINEFUNCTIONINFO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include <optional>

namespace llvm {
class RISCCMachineFunctionInfo final : public MachineFunctionInfo {
public:
  struct ArgumentExtension {
    MCRegister Reg;
    unsigned Bits;
    bool Signed;
  };

private:
  // ABI facts about entry registers. They describe the incoming values, not
  // later uses of those physical registers or arbitrary private arguments.
  SmallVector<ArgumentExtension, 6> ArgumentExtensions;
  int LRSpillFI = -1;
  int BranchRelaxationSpillFI = -1;
  int VarArgsFrameIndex = 0;
  Register ReturnAddressReg;
  std::optional<bool> EntryLinkIsLive;
  MCRegister LRSaveReg;
  MCRegister R4SaveReg;
  MCRegister R5SaveReg;
  MCRegister R6SaveReg;

public:
  RISCCMachineFunctionInfo() = default;
  explicit RISCCMachineFunctionInfo(const Function &,
                                    const TargetSubtargetInfo *);
  void addArgumentExtension(MCRegister Reg, unsigned Bits, bool Signed) {
    ArgumentExtensions.push_back({Reg, Bits, Signed});
  }
  ArrayRef<ArgumentExtension> getArgumentExtensions() const {
    return ArgumentExtensions;
  }
  int getLRSpillFI() const { return LRSpillFI; }
  void setLRSpillFI(int FI) { LRSpillFI = FI; }
  int getBranchRelaxationSpillFI() const { return BranchRelaxationSpillFI; }
  void setBranchRelaxationSpillFI(int FI) { BranchRelaxationSpillFI = FI; }
  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }
  Register getReturnAddressReg() const { return ReturnAddressReg; }
  void setReturnAddressReg(Register Reg) { ReturnAddressReg = Reg; }
  std::optional<bool> getEntryLinkIsLive() const { return EntryLinkIsLive; }
  void setEntryLinkIsLive(bool Live) { EntryLinkIsLive = Live; }
  MCRegister getLRSaveReg() const { return LRSaveReg; }
  void setLRSaveReg(MCRegister Reg) { LRSaveReg = Reg; }
  void clearSRegPlan() {
    EntryLinkIsLive.reset();
    LRSaveReg = MCRegister();
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
