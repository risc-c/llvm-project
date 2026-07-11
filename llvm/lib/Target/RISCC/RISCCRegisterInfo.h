#ifndef LLVM_LIB_TARGET_RISCC_RISCCREGISTERINFO_H
#define LLVM_LIB_TARGET_RISCC_RISCCREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "RISCCGenRegisterInfo.inc"

namespace llvm {
class RISCCRegisterInfo final : public RISCCGenRegisterInfo {
public:
  RISCCRegisterInfo();
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
