//===-- RISCCPrepare.h - Prepare IR for RISC-C codegen -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_TARGET_RISCC_RISCCPREPARE_H
#define LLVM_LIB_TARGET_RISCC_RISCCPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class FunctionPass;
class PassRegistry;
class RISCCPreparePass : public PassInfoMixin<RISCCPreparePass> {
public:
  PreservedAnalyses run(Function &, FunctionAnalysisManager &);
};
FunctionPass *createRISCCPrepareLegacyPass();
void initializeRISCCPrepareLegacyPass(PassRegistry &);
} // namespace llvm
#endif
