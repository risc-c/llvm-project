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
  void anchor();

public:
  explicit RISCCInstrInfo(const RISCCSubtarget &);
  const RISCCRegisterInfo &getRegisterInfo() const { return RI; }

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
  unsigned getInstSizeInBytes(const MachineInstr &) const override;
  bool reverseBranchCondition(SmallVectorImpl<MachineOperand> &) const override;
  bool analyzeBranch(MachineBasicBlock &, MachineBasicBlock *&,
                     MachineBasicBlock *&, SmallVectorImpl<MachineOperand> &,
                     bool) const override;
  unsigned removeBranch(MachineBasicBlock &, int * = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &, MachineBasicBlock *,
                        MachineBasicBlock *, ArrayRef<MachineOperand>,
                        const DebugLoc &, int * = nullptr) const override;
};
}

#endif
