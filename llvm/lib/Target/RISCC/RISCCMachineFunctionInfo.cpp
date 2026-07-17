//===-- RISCCMachineFunctionInfo.cpp - RISCC Function Information ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCMachineFunctionInfo.h"

using namespace llvm;

void RISCCMachineFunctionInfo::anchor() {}

MachineFunctionInfo *RISCCMachineFunctionInfo::create(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) {
  return new (Allocator.Allocate<RISCCMachineFunctionInfo>())
      RISCCMachineFunctionInfo(F, STI);
}
