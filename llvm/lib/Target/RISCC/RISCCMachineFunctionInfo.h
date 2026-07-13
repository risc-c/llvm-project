#ifndef LLVM_LIB_TARGET_RISCC_RISCCMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_RISCC_RISCCMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {
class RISCCMachineFunctionInfo final : public MachineFunctionInfo {
  int LRSpillFI = -1;
  int VarArgsFrameIndex = 0;

public:
  RISCCMachineFunctionInfo() = default;
  explicit RISCCMachineFunctionInfo(const Function &, const TargetSubtargetInfo *) {}
  int getLRSpillFI() const { return LRSpillFI; }
  void setLRSpillFI(int FI) { LRSpillFI = FI; }
  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }
  virtual void anchor();
  static MachineFunctionInfo *create(BumpPtrAllocator &, const Function &,
                                     const TargetSubtargetInfo *);
};
}

#endif
