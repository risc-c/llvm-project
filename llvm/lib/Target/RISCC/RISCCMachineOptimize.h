//===-- RISCCMachineOptimize.h - Machine SSA optimizations -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_TARGET_RISCC_RISCCMACHINEOPTIMIZE_H
#define LLVM_LIB_TARGET_RISCC_RISCCMACHINEOPTIMIZE_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {
class FunctionPass;
class PassRegistry;
class RISCCMachineOptimizePass
    : public PassInfoMixin<RISCCMachineOptimizePass> {
public:
  PreservedAnalyses run(MachineFunction &, MachineFunctionAnalysisManager &);
};
FunctionPass *createRISCCMachineOptimizeLegacyPass();
void initializeRISCCMachineOptimizeLegacyPass(PassRegistry &);
} // namespace llvm
#endif
