//===-- RISCCSRegAllocator.h - Plan use of the S-register cache -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCSREGALLOCATOR_H
#define LLVM_LIB_TARGET_RISCC_RISCCSREGALLOCATOR_H

#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class FunctionPass;
class PassRegistry;

class RISCCSRegAllocatorPass
    : public PassInfoMixin<RISCCSRegAllocatorPass> {
public:
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
};

FunctionPass *createRISCCSRegAllocatorLegacyPass();
void initializeRISCCSRegAllocatorLegacyPass(PassRegistry &);

} // namespace llvm

#endif
