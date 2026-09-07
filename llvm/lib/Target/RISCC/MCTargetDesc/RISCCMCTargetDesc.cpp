//===-- RISCCMCTargetDesc.cpp - RISCC Target Descriptions -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCMCTargetDesc.h"
#include "RISCCInstPrinter.h"
#include "RISCCMCAsmInfo.h"
#include "TargetInfo/RISCCTargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "RISCCGenInstrInfo.inc"
#define GET_REGINFO_MC_DESC
#include "RISCCGenRegisterInfo.inc"
#define GET_SUBTARGETINFO_MC_DESC
#include "RISCCGenSubtargetInfo.inc"

static MCInstrInfo *createRISCCMCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitRISCCMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createRISCCMCRegisterInfo(const Triple &) {
  auto *X = new MCRegisterInfo();
  InitRISCCMCRegisterInfo(X, RISCC::S7);
  return X;
}

static MCAsmInfo *createRISCCMCAsmInfo(const MCRegisterInfo &, const Triple &TT,
                                       const MCTargetOptions &Options) {
  return new RISCCMCAsmInfo(TT, Options);
}

static MCObjectFileInfo *createRISCCMCObjectFileInfo(MCContext &Ctx, bool PIC,
                                                     bool LargeCodeModel) {
  auto &MAI =
      static_cast<RISCCMCAsmInfo &>(const_cast<MCAsmInfo &>(Ctx.getAsmInfo()));
  MAI.setRC32(Ctx.getSubtargetInfo()->hasFeature(RISCC::FeatureRC32));
  auto *MOFI = new MCObjectFileInfo();
  MOFI->initMCObjectFileInfo(Ctx, PIC, LargeCodeModel);
  return MOFI;
}

static MCSubtargetInfo *
createRISCCMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "full";
  return createRISCCMCSubtargetInfoImpl(TT, CPU, CPU, FS);
}

static MCInstPrinter *createRISCCMCInstPrinter(const Triple &, unsigned Variant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return Variant == 0 ? new RISCCInstPrinter(MAI, MII, MRI) : nullptr;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeRISCCTargetMC() {
  Target &T = getTheRISCCTarget();
  TargetRegistry::RegisterMCAsmInfo(T, createRISCCMCAsmInfo);
  TargetRegistry::RegisterMCObjectFileInfo(T, createRISCCMCObjectFileInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createRISCCMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createRISCCMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createRISCCMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createRISCCMCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createRISCCMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createRISCCMCAsmBackend);
  TargetRegistry::RegisterELFStreamer(T, createRISCCELFStreamer);
  TargetRegistry::RegisterObjectTargetStreamer(T,
                                               createRISCCObjectTargetStreamer);
}
