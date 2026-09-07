//===-- RISCCSubtarget.h - RISCC Subtarget Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCSUBTARGET_H
#define LLVM_LIB_TARGET_RISCC_RISCCSUBTARGET_H

#include "RISCCFrameLowering.h"
#include "RISCCISelLowering.h"
#include "RISCCInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGenTypes/MachineValueType.h"

#define GET_SUBTARGETINFO_HEADER
#include "RISCCGenSubtargetInfo.inc"

namespace llvm {
class RISCCSubtarget final : public RISCCGenSubtargetInfo {
  bool HasSys = false;
  bool HasLongJall = false;
  bool HasWideShift = false;
  bool HasMul = false;
  bool HasMulhu = false;
  bool HasDivu = false;
  bool IsNano = false;
  bool IsRC32 = false;
  RISCCInstrInfo InstrInfo;
  RISCCFrameLowering FrameLowering;
  RISCCTargetLowering TLInfo;
  std::unique_ptr<const SelectionDAGTargetInfo> TSInfo;

public:
  RISCCSubtarget(const Triple &, const std::string &CPU, const std::string &FS,
                 const TargetMachine &);
  ~RISCCSubtarget() override;
  RISCCSubtarget &initializeSubtargetDependencies(StringRef CPU, StringRef FS);
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  bool hasSys() const { return HasSys; }
  bool hasLongJall() const { return HasLongJall; }
  bool hasWideShift() const { return HasWideShift; }
  bool hasMul() const { return HasMul; }
  bool hasMulhu() const { return HasMulhu; }
  bool hasDivu() const { return HasDivu; }
  bool isNano() const { return IsNano; }
  bool isRC32() const { return IsRC32; }
  MVT getXLenVT() const { return IsRC32 ? MVT::i32 : MVT::i16; }
  unsigned getSlotSize() const { return IsRC32 ? 4 : 2; }
  Align getStackAlignment() const { return Align(getSlotSize()); }
  const TargetRegisterClass *getGPRClass() const {
    return IsRC32 ? &RISCC::GPR32RegClass : &RISCC::GPRRegClass;
  }
  const TargetRegisterClass *getSRegClass() const {
    return IsRC32 ? &RISCC::SREG32RegClass : &RISCC::SREGRegClass;
  }
  const RISCCInstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const RISCCRegisterInfo *getRegisterInfo() const override;
  const RISCCFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const RISCCTargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override;
  void initLibcallLoweringInfo(LibcallLoweringInfo &Info) const override;
};
} // namespace llvm

#endif
