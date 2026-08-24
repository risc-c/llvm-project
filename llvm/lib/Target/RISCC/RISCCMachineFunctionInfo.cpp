//===-- RISCCMachineFunctionInfo.cpp - RISCC Function Information ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCC.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/IR/Function.h"

using namespace llvm;

MCRegister llvm::getRISCCMainlineLinkRegister(const Function &F) {
  return F.hasLocalLinkage() && !F.hasAddressTaken() ? RISCC::S3 : RISCC::S7;
}

RISCCMachineFunctionInfo::RISCCMachineFunctionInfo(
    const Function &F, const TargetSubtargetInfo *STI) {
  const auto &Subtarget = *static_cast<const RISCCSubtarget *>(STI);
  if (!Subtarget.isNano())
    // RC32 currently uses the public S7 convention for every function. The
    // RC16-only private-link convention is selected together at both the
    // caller and callee; applying only its callee half to RC32 would make
    // local functions return through an uninitialized S3.
    ReturnAddressReg =
        Subtarget.isRC32() ? RISCC::S7 : getRISCCMainlineLinkRegister(F);
}

void RISCCMachineFunctionInfo::setCalleeSavedSReg(MCRegister GPR,
                                                  MCRegister SReg) {
  assert((GPR == RISCC::R4 || GPR == RISCC::R5 || GPR == RISCC::R6) &&
         "only callee-saved GPRs have S-register backups");
  if (GPR == RISCC::R4)
    R4SaveReg = SReg;
  else if (GPR == RISCC::R5)
    R5SaveReg = SReg;
  else
    R6SaveReg = SReg;
}

MCRegister
RISCCMachineFunctionInfo::getCalleeSavedSReg(MCRegister GPR) const {
  assert((GPR == RISCC::R4 || GPR == RISCC::R5 || GPR == RISCC::R6) &&
         "only callee-saved GPRs have S-register backups");
  if (GPR == RISCC::R4)
    return R4SaveReg;
  return GPR == RISCC::R5 ? R5SaveReg : R6SaveReg;
}

void RISCCMachineFunctionInfo::anchor() {}

MachineFunctionInfo *RISCCMachineFunctionInfo::create(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) {
  return new (Allocator.Allocate<RISCCMachineFunctionInfo>())
      RISCCMachineFunctionInfo(F, STI);
}
