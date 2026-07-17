//===-- RISCCMCAsmInfo.cpp - RISCC Assembly Properties --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCMCAsmInfo.h"

using namespace llvm;

void RISCCMCAsmInfo::anchor() {}

RISCCMCAsmInfo::RISCCMCAsmInfo(const Triple &, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 2;
  CalleeSaveStackSlotSize = 2;
  MinInstAlignment = 2;
  MaxInstLength = 6;
  CommentString = ";";
  AlignmentIsInBytes = true;
  UsesELFSectionDirectiveForBSS = true;
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::None;
}
