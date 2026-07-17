//===-- RISCCELFStreamer.cpp - RISCC ELF Streamer -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCFixupKinds.h"
#include "RISCCMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"

using namespace llvm;

namespace {
class RISCCELFStreamer final : public MCELFStreamer {
public:
  RISCCELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
                   std::unique_ptr<MCObjectWriter> MOW,
                   std::unique_ptr<MCCodeEmitter> MCE)
      : MCELFStreamer(Context, std::move(MAB), std::move(MOW),
                      std::move(MCE)) {}

  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override {
    MCFragment *Fragment = getCurrentFragment();
    size_t Offset = getCurFragSize();
    MCELFStreamer::emitInstruction(Inst, STI);

    // The base streamer may append into a fresh fragment. Mark the actual
    // instruction start so the assembler can diagnose odd code addresses.
    if (Fragment != getCurrentFragment()) {
      Fragment = getCurrentFragment();
      Offset = 0;
    }
    if (getCurFragSize() == Offset)
      return;

    Fragment->addFixup(MCFixup::create(
        Offset, MCConstantExpr::create(0, getContext()),
        RISCC::fixup_insn_align));
  }
};

class RISCCTargetELFStreamer final : public MCTargetStreamer {
public:
  RISCCTargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI)
      : MCTargetStreamer(S) {
    auto &ES = static_cast<MCELFStreamer &>(Streamer);
    unsigned Profile = ELF::EF_RISCC_PROFILE_MIN;
    if (STI.hasFeature(RISCC::FeatureSys) ||
        STI.hasFeature(RISCC::FeatureWideShift))
      Profile = ELF::EF_RISCC_PROFILE_SYS;
    if (STI.hasFeature(RISCC::FeatureMul))
      Profile = ELF::EF_RISCC_PROFILE_FULL;
    ES.getWriter().setELFHeaderEFlags(ELF::EF_RISCC_ABI_V1 | Profile);
  }
};
}

MCStreamer *llvm::createRISCCELFStreamer(
    const Triple &, MCContext &Context, std::unique_ptr<MCAsmBackend> &&MAB,
    std::unique_ptr<MCObjectWriter> &&MOW,
    std::unique_ptr<MCCodeEmitter> &&MCE) {
  return new RISCCELFStreamer(Context, std::move(MAB), std::move(MOW),
                              std::move(MCE));
}

MCTargetStreamer *llvm::createRISCCObjectTargetStreamer(
    MCStreamer &S, const MCSubtargetInfo &STI) {
  if (STI.getTargetTriple().isOSBinFormatELF())
    return new RISCCTargetELFStreamer(S, STI);
  return nullptr;
}
