#ifndef LLVM_LIB_TARGET_RISCC_RISCCFRAMELOWERING_H
#define LLVM_LIB_TARGET_RISCC_RISCCFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
class RISCCSubtarget;
class RISCCFrameLowering final : public TargetFrameLowering {
  const RISCCSubtarget &STI;
  bool hasFPImpl(const MachineFunction &) const override { return false; }

public:
  explicit RISCCFrameLowering(const RISCCSubtarget &);
  void emitPrologue(MachineFunction &, MachineBasicBlock &) const override;
  void emitEpilogue(MachineFunction &, MachineBasicBlock &) const override;
  bool hasReservedCallFrame(const MachineFunction &) const override {
    return true;
  }
  MachineBasicBlock::iterator eliminateCallFramePseudoInstr(
      MachineFunction &, MachineBasicBlock &,
      MachineBasicBlock::iterator) const override;
  void processFunctionBeforeFrameFinalized(MachineFunction &,
                                            RegScavenger *) const override;
};
}

#endif
