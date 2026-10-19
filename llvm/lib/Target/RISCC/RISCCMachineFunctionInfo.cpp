//===-- RISCCMachineFunctionInfo.cpp - RISCC Function Information ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCMachineFunctionInfo.h"
#include "RISCC.h"
#include "RISCCSubtarget.h"
#include "llvm/IR/Function.h"

using namespace llvm;

MCRegister llvm::getRISCCMainlineLinkRegister(const Function &F) {
  if (F.getFnAttribute("riscc-link-register").getValueAsString() == "s7")
    return RISCC::S7;
  return F.hasLocalLinkage() && !F.hasAddressTaken() ? RISCC::S3 : RISCC::S7;
}

unsigned llvm::getRISCCArgumentRegisterCount(const Function &F,
                                             const RISCCSubtarget &STI) {
  unsigned Count;
  StringRef Value =
      F.getFnAttribute("riscc-private-reg-args").getValueAsString();
  if (Value.getAsInteger(10, Count) || Count < 3 || Count > 6)
    return 3;
  // llc's CPU need not appear in IR attributes. Nano's link always reserves R6.
  return STI.isNano() ? std::min(Count, 5u) : Count;
}

RISCCMachineFunctionInfo::RISCCMachineFunctionInfo(
    const Function &F, const TargetSubtargetInfo *STI) {
  const auto &Subtarget = *static_cast<const RISCCSubtarget *>(STI);
  if (!Subtarget.isNano())
    ReturnAddressReg = getRISCCMainlineLinkRegister(F);
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

MCRegister RISCCMachineFunctionInfo::getCalleeSavedSReg(MCRegister GPR) const {
  assert((GPR == RISCC::R4 || GPR == RISCC::R5 || GPR == RISCC::R6) &&
         "only callee-saved GPRs have S-register backups");
  if (GPR == RISCC::R4)
    return R4SaveReg;
  return GPR == RISCC::R5 ? R5SaveReg : R6SaveReg;
}

void RISCCMachineFunctionInfo::anchor() {}

MachineFunctionInfo *
RISCCMachineFunctionInfo::create(BumpPtrAllocator &Allocator, const Function &F,
                                 const TargetSubtargetInfo *STI) {
  return new (Allocator.Allocate<RISCCMachineFunctionInfo>())
      RISCCMachineFunctionInfo(F, STI);
}
