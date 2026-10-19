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
  // Select a native-width opcode using the RC16 member of a NativeInst pair.
  // This maps types, not feature availability (including Nano restrictions).
  unsigned getNativeOpcode(unsigned RC16Opcode) const;
  unsigned getTailMergeSize(const MachineFunction &) const override;
  bool enableSizeAwareBlockPlacement(const MachineFunction &) const override;
  static bool isConditionalBranchOpcode(unsigned Opcode);
  static unsigned getOppositeBranchOpcode(unsigned Opcode);
  void materializeImmediate(MachineBasicBlock &, MachineBasicBlock::iterator,
                            const DebugLoc &, Register, int64_t,
                            MachineInstr::MIFlag = MachineInstr::NoFlags) const;

  void copyPhysReg(MachineBasicBlock &, MachineBasicBlock::iterator,
                   const DebugLoc &, Register, Register, bool, bool = false,
                   bool = false) const override;
  std::optional<DestSourcePair>
  isCopyInstrImpl(const MachineInstr &) const override;
  bool simplifyInstruction(MachineInstr &) const override;
  bool isReMaterializableImpl(const MachineInstr &) const override;
  bool isFunctionSafeToOutlineFrom(MachineFunction &, bool) const override;
  bool shouldOutlineFromFunctionByDefault(MachineFunction &) const override;
  bool isMBBSafeToOutlineFrom(MachineBasicBlock &, unsigned &) const override;
  outliner::InstrType getOutliningTypeImpl(const MachineModuleInfo &,
                                           MachineBasicBlock::iterator &,
                                           unsigned) const override;
  std::optional<std::unique_ptr<outliner::OutlinedFunction>>
  getOutliningCandidateInfo(const MachineModuleInfo &,
                            std::vector<outliner::Candidate> &,
                            unsigned) const override;
  void buildOutlinedFrame(MachineBasicBlock &, MachineFunction &,
                          const outliner::OutlinedFunction &) const override;
  MachineBasicBlock::iterator
  insertOutlinedCall(Module &, MachineBasicBlock &,
                     MachineBasicBlock::iterator &, MachineFunction &,
                     outliner::Candidate &) const override;
  void storeRegToStackSlot(MachineBasicBlock &, MachineBasicBlock::iterator,
                           Register, bool, int, const TargetRegisterClass *,
                           Register, MachineInstr::MIFlag) const override;
  void loadRegFromStackSlot(MachineBasicBlock &, MachineBasicBlock::iterator,
                            Register, int, const TargetRegisterClass *,
                            Register, unsigned,
                            MachineInstr::MIFlag) const override;
  Register isLoadFromStackSlot(const MachineInstr &, int &) const override;
  Register isStoreToStackSlot(const MachineInstr &, int &) const override;
  Register isLoadFromStackSlot(const MachineInstr &, int &,
                               TypeSize &) const override;
  Register isStoreToStackSlot(const MachineInstr &, int &,
                              TypeSize &) const override;
  bool expandPostRAPseudo(MachineInstr &) const override;
  bool getMemOperandsWithOffsetWidth(const MachineInstr &,
                                     SmallVectorImpl<const MachineOperand *> &,
                                     int64_t &, bool &, LocationSize &,
                                     const TargetRegisterInfo *) const override;
  bool areMemAccessesTriviallyDisjoint(const MachineInstr &,
                                       const MachineInstr &) const override;
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
} // namespace llvm

#endif
