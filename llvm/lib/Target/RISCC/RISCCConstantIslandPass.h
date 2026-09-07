//===-- RISCCConstantIslandPass.h - RISC-C literal layout -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCCONSTANTISLANDPASS_H
#define LLVM_LIB_TARGET_RISCC_RISCCCONSTANTISLANDPASS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {
class FunctionPass;
class PassRegistry;

class RISCCConstantIslandPass
    : public RequiredPassInfoMixin<RISCCConstantIslandPass> {
public:
  PreservedAnalyses run(MachineFunction &, MachineFunctionAnalysisManager &);
};

FunctionPass *createRISCCConstantIslandPass();
void initializeRISCCConstantIslandLegacyPass(PassRegistry &);
} // namespace llvm

#endif
