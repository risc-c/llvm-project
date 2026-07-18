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

      {RTLIB::MUL_I32, RTLIB::impl___mulsi3},
      {RTLIB::SHL_I32, RTLIB::impl___ashlsi3},
      {RTLIB::SRL_I32, RTLIB::impl___lshrsi3},
      {RTLIB::SRA_I32, RTLIB::impl___ashrsi3},
      {RTLIB::SDIV_I32, RTLIB::impl___divsi3},
      {RTLIB::UDIV_I32, RTLIB::impl___udivsi3},
      {RTLIB::SREM_I32, RTLIB::impl___modsi3},
      {RTLIB::UREM_I32, RTLIB::impl___umodsi3},

      {RTLIB::MUL_I64, RTLIB::impl___muldi3},
      {RTLIB::SHL_I64, RTLIB::impl___ashldi3},
      {RTLIB::SRL_I64, RTLIB::impl___lshrdi3},
      {RTLIB::SRA_I64, RTLIB::impl___ashrdi3},
      {RTLIB::SDIV_I64, RTLIB::impl___divdi3},
      {RTLIB::UDIV_I64, RTLIB::impl___udivdi3},
      {RTLIB::SREM_I64, RTLIB::impl___moddi3},
      {RTLIB::UREM_I64, RTLIB::impl___umoddi3},

      {RTLIB::ADD_F32, RTLIB::impl___addsf3},
      {RTLIB::SUB_F32, RTLIB::impl___subsf3},
      {RTLIB::MUL_F32, RTLIB::impl___mulsf3},
      {RTLIB::DIV_F32, RTLIB::impl___divsf3},
      {RTLIB::ADD_F64, RTLIB::impl___adddf3},
      {RTLIB::SUB_F64, RTLIB::impl___subdf3},
      {RTLIB::MUL_F64, RTLIB::impl___muldf3},
      {RTLIB::DIV_F64, RTLIB::impl___divdf3},

      {RTLIB::FPEXT_F32_F64, RTLIB::impl___extendsfdf2},
      {RTLIB::FPROUND_F64_F32, RTLIB::impl___truncdfsf2},
      {RTLIB::FPTOSINT_F32_I32, RTLIB::impl___fixsfsi},
      {RTLIB::FPTOSINT_F32_I64, RTLIB::impl___fixsfdi},
      {RTLIB::FPTOSINT_F64_I32, RTLIB::impl___fixdfsi},
      {RTLIB::FPTOSINT_F64_I64, RTLIB::impl___fixdfdi},
      {RTLIB::FPTOUINT_F32_I32, RTLIB::impl___fixunssfsi},
      {RTLIB::FPTOUINT_F32_I64, RTLIB::impl___fixunssfdi},
      {RTLIB::FPTOUINT_F64_I32, RTLIB::impl___fixunsdfsi},
      {RTLIB::FPTOUINT_F64_I64, RTLIB::impl___fixunsdfdi},
      {RTLIB::SINTTOFP_I32_F32, RTLIB::impl___floatsisf},
      {RTLIB::SINTTOFP_I32_F64, RTLIB::impl___floatsidf},
      {RTLIB::SINTTOFP_I64_F32, RTLIB::impl___floatdisf},
      {RTLIB::SINTTOFP_I64_F64, RTLIB::impl___floatdidf},
      {RTLIB::UINTTOFP_I32_F32, RTLIB::impl___floatunsisf},
      {RTLIB::UINTTOFP_I32_F64, RTLIB::impl___floatunsidf},
      {RTLIB::UINTTOFP_I64_F32, RTLIB::impl___floatundisf},
      {RTLIB::UINTTOFP_I64_F64, RTLIB::impl___floatundidf},

      {RTLIB::OEQ_F32, RTLIB::impl___eqsf2},
      {RTLIB::UNE_F32, RTLIB::impl___nesf2},
      {RTLIB::OGE_F32, RTLIB::impl___gesf2},
      {RTLIB::OLT_F32, RTLIB::impl___ltsf2},
      {RTLIB::OLE_F32, RTLIB::impl___lesf2},
      {RTLIB::OGT_F32, RTLIB::impl___gtsf2},
      {RTLIB::UO_F32, RTLIB::impl___unordsf2},
      {RTLIB::OEQ_F64, RTLIB::impl___eqdf2},
      {RTLIB::UNE_F64, RTLIB::impl___nedf2},
      {RTLIB::OGE_F64, RTLIB::impl___gedf2},
      {RTLIB::OLT_F64, RTLIB::impl___ltdf2},
      {RTLIB::OLE_F64, RTLIB::impl___ledf2},
      {RTLIB::OGT_F64, RTLIB::impl___gtdf2},
      {RTLIB::UO_F64, RTLIB::impl___unorddf2},

      {RTLIB::REM_F32, RTLIB::impl_fmodf},
      {RTLIB::SQRT_F32, RTLIB::impl_sqrtf},
      {RTLIB::CEIL_F32, RTLIB::impl_ceilf},
      {RTLIB::TRUNC_F32, RTLIB::impl_truncf},
      {RTLIB::ROUND_F32, RTLIB::impl_roundf},
      {RTLIB::FLOOR_F32, RTLIB::impl_floorf},
      {RTLIB::REM_F64, RTLIB::impl_fmod},
      {RTLIB::SQRT_F64, RTLIB::impl_sqrt},
      {RTLIB::CEIL_F64, RTLIB::impl_ceil},
      {RTLIB::TRUNC_F64, RTLIB::impl_trunc},
      {RTLIB::ROUND_F64, RTLIB::impl_round},
      {RTLIB::FLOOR_F64, RTLIB::impl_floor},
  };
  for (const auto &Helper : Helpers)
    Info.setLibcallImpl(Helper.Op, Helper.Impl);
}
