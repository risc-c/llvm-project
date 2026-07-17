//===-- RISCCSubtarget.cpp - RISCC Subtarget Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCSubtarget.h"
#include "llvm/CodeGen/LibcallLoweringInfo.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

using namespace llvm;

#define DEBUG_TYPE "riscc-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "RISCCGenSubtargetInfo.inc"

RISCCSubtarget &RISCCSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                                StringRef FS) {
  if (CPU.empty())
    CPU = "full";
  ParseSubtargetFeatures(CPU, CPU, FS);
  return *this;
}

RISCCSubtarget::RISCCSubtarget(const Triple &TT, const std::string &CPU,
                               const std::string &FS, const TargetMachine &TM)
    : RISCCGenSubtargetInfo(TT, CPU, CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), FrameLowering(*this),
      TLInfo(TM, *this), TSInfo(std::make_unique<SelectionDAGTargetInfo>()) {}

RISCCSubtarget::~RISCCSubtarget() = default;

const RISCCRegisterInfo *RISCCSubtarget::getRegisterInfo() const {
  return &InstrInfo.getRegisterInfo();
}

const SelectionDAGTargetInfo *RISCCSubtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}

void RISCCSubtarget::initLibcallLoweringInfo(
    LibcallLoweringInfo &Info) const {
  // A new bare-metal architecture is not part of LLVM's generic system
  // library availability tables.  Select the compiler-rt/libgcc spellings
  // explicitly; the freestanding RISC-C runtime supplies these entry points.
  static constexpr struct {
    RTLIB::Libcall Op;
    RTLIB::LibcallImpl Impl;
  } Helpers[] = {
      {RTLIB::MEMCPY, RTLIB::impl_memcpy},
      {RTLIB::MEMMOVE, RTLIB::impl_memmove},
      {RTLIB::MEMSET, RTLIB::impl_memset},

      {RTLIB::MUL_I16, RTLIB::impl___mulhi3},
      {RTLIB::SDIV_I16, RTLIB::impl___divhi3},
      {RTLIB::UDIV_I16, RTLIB::impl___udivhi3},
      {RTLIB::SREM_I16, RTLIB::impl___modhi3},
      {RTLIB::UREM_I16, RTLIB::impl___umodhi3},
      {RTLIB::SDIVREM_I16, RTLIB::impl___divmodhi4},
      {RTLIB::UDIVREM_I16, RTLIB::impl___udivmodhi4},

      {RTLIB::MUL_I32, RTLIB::impl___mulsi3},
      {RTLIB::SHL_I32, RTLIB::impl___ashlsi3},
      {RTLIB::SRL_I32, RTLIB::impl___lshrsi3},
      {RTLIB::SRA_I32, RTLIB::impl___ashrsi3},
      {RTLIB::SDIV_I32, RTLIB::impl___divsi3},
      {RTLIB::UDIV_I32, RTLIB::impl___udivsi3},
      {RTLIB::SREM_I32, RTLIB::impl___modsi3},
      {RTLIB::UREM_I32, RTLIB::impl___umodsi3},
      {RTLIB::SDIVREM_I32, RTLIB::impl___divmodsi4},
      {RTLIB::UDIVREM_I32, RTLIB::impl___udivmodsi4},

      {RTLIB::MUL_I64, RTLIB::impl___muldi3},
      {RTLIB::SHL_I64, RTLIB::impl___ashldi3},
      {RTLIB::SRL_I64, RTLIB::impl___lshrdi3},
      {RTLIB::SRA_I64, RTLIB::impl___ashrdi3},
      {RTLIB::SDIV_I64, RTLIB::impl___divdi3},
      {RTLIB::UDIV_I64, RTLIB::impl___udivdi3},
      {RTLIB::SREM_I64, RTLIB::impl___moddi3},
      {RTLIB::UREM_I64, RTLIB::impl___umoddi3},
  };
  for (const auto &Helper : Helpers)
    Info.setLibcallImpl(Helper.Op, Helper.Impl);
}
