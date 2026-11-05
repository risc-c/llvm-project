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
class MachineInstr;
template <typename T> class SmallVectorImpl;
bool enableRISCCConstantBranches();
bool coalesceRISCCLocalCopies(MachineFunction &);
bool placeRISCCAddsOperands(MachineFunction &);
bool foldRISCCAdds(MachineFunction &);
bool findRISCCAddsUsers(MachineInstr &, SmallVectorImpl<MachineInstr *> &Uses,
                        SmallVectorImpl<MachineInstr *> &DebugUses);
class RISCCMachineOptimizePass
    : public PassInfoMixin<RISCCMachineOptimizePass> {
public:
  PreservedAnalyses run(MachineFunction &, MachineFunctionAnalysisManager &);
};
FunctionPass *createRISCCMachineOptimizeLegacyPass();
void initializeRISCCMachineOptimizeLegacyPass(PassRegistry &);
class RISCCPostRAOptimizePass : public PassInfoMixin<RISCCPostRAOptimizePass> {
public:
  PreservedAnalyses run(MachineFunction &, MachineFunctionAnalysisManager &);
};
FunctionPass *createRISCCPostRAOptimizeLegacyPass();
void initializeRISCCPostRAOptimizeLegacyPass(PassRegistry &);
class RISCCReturnOptimizePass : public PassInfoMixin<RISCCReturnOptimizePass> {
public:
  PreservedAnalyses run(MachineFunction &, MachineFunctionAnalysisManager &);
};
FunctionPass *createRISCCReturnOptimizeLegacyPass();
void initializeRISCCReturnOptimizeLegacyPass(PassRegistry &);
} // namespace llvm
#endif
