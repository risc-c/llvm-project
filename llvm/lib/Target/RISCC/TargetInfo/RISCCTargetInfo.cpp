//===-- RISCCTargetInfo.cpp - RISCC Target Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

Target &llvm::getTheRISCCTarget() {
  static Target T;
  return T;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeRISCCTargetInfo() {
  RegisterTarget<Triple::riscc> X(getTheRISCCTarget(), "riscc",
                                  "RISC-C 16-bit [experimental]", "RISCC");
}
