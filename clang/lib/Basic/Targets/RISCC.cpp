//===--- RISCC.cpp - Implement RISC-C target feature support -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCC.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

const char *const RISCCTargetInfo::GCCRegNames[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
};

const TargetInfo::GCCRegAlias RISCCTargetInfo::GCCRegAliases[] = {
    {{"S0"}, "s0"}, {{"S1"}, "s1"}, {{"S2"}, "s2"},
    {{"S3"}, "s3"}, {{"S4"}, "s4"}, {{"S5"}, "s5"},
    {{"S6"}, "s6"}, {{"S7"}, "s7"},
};

ArrayRef<const char *> RISCCTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::GCCRegAlias> RISCCTargetInfo::getGCCRegAliases() const {
  return llvm::ArrayRef(GCCRegAliases);
}

bool RISCCTargetInfo::isValidCPUName(StringRef Name) const {
  return Name == "min" || Name == "sys" || Name == "full";
}

void RISCCTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  Values.emplace_back("min");
  Values.emplace_back("sys");
  Values.emplace_back("full");
}

bool RISCCTargetInfo::setCPU(StringRef Name) {
  if (!isValidCPUName(Name))
    return false;
  CPU = Name;
  return true;
}

bool RISCCTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("riscc", true)
      .Case("min", CPU == "min")
      .Case("full", CPU == "full")
      .Cases({"sys", "system", "jal16"}, CPU != "min")
      .Cases({"wide-shift", "wide-shifts"}, CPU != "min")
      .Case("mul", CPU == "full")
      .Default(false);
}

void RISCCTargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  Builder.defineMacro("__riscc__");
  Builder.defineMacro("__RISCC__");
  if (CPU == "min")
    Builder.defineMacro("__RISCC_MIN__");
  else if (CPU == "sys")
    Builder.defineMacro("__RISCC_SYS__");
  else
    Builder.defineMacro("__RISCC_FULL__");
}
