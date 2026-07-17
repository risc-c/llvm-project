//===-- RISCCMCAsmInfo.h - RISCC Assembly Properties ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCMCASMINFO_H
#define LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;
class RISCCMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;
public:
  RISCCMCAsmInfo(const Triple &, const MCTargetOptions &);
};
}

#endif
