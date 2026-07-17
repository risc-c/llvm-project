//===-- RISCCTargetInfo.h - RISCC Target Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_TARGETINFO_RISCCTARGETINFO_H
#define LLVM_LIB_TARGET_RISCC_TARGETINFO_RISCCTARGETINFO_H

namespace llvm {
class Target;
Target &getTheRISCCTarget();
}

#endif
