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
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"

using namespace llvm;

namespace {
unsigned getELFProfile(const MCSubtargetInfo &STI) {
  if (STI.hasFeature(RISCC::FeatureNano))
    return ELF::EF_RISCC_PROFILE_NANO;
  if (STI.hasFeature(RISCC::FeatureMul) ||
      STI.hasFeature(RISCC::FeatureWideShift))
    return ELF::EF_RISCC_PROFILE_FULL;
  if (STI.hasFeature(RISCC::FeatureSys) ||
      STI.hasFeature(RISCC::FeatureLongJall))
    return ELF::EF_RISCC_PROFILE_SYS;
  return ELF::EF_RISCC_PROFILE_MIN;
}

class RISCCELFStreamer final : public MCELFStreamer {
public:
  RISCCELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
                   std::unique_ptr<MCObjectWriter> MOW,
                   std::unique_ptr<MCCodeEmitter> MCE)
      : MCELFStreamer(Context, std::move(MAB), std::move(MOW), std::move(MCE)) {
  }

  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override {
    unsigned Flags = getWriter().getELFHeaderEFlags();
    unsigned Profile = Flags & ELF::EF_RISCC_PROFILE_MASK;
    unsigned RequiredProfile = getELFProfile(STI);
    // Mainline capability order is Min < Sys < Full, unlike the flag values.
    if (Profile != ELF::EF_RISCC_PROFILE_NANO &&
        (RequiredProfile == ELF::EF_RISCC_PROFILE_FULL ||
         (RequiredProfile == ELF::EF_RISCC_PROFILE_SYS &&
          Profile == ELF::EF_RISCC_PROFILE_MIN)))
      getWriter().setELFHeaderEFlags((Flags & ~ELF::EF_RISCC_PROFILE_MASK) |
                                     RequiredProfile);

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

    Fragment->addFixup(MCFixup::create(Offset,
                                       MCConstantExpr::create(0, getContext()),
                                       RISCC::fixup_insn_align));
  }
};

class RISCCTargetELFStreamer final : public MCTargetStreamer {
public:
  RISCCTargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI)
      : MCTargetStreamer(S) {
    auto &ES = static_cast<MCELFStreamer &>(Streamer);
    unsigned Profile = getELFProfile(STI);
    unsigned Config = STI.hasFeature(RISCC::FeatureRC32)
                          ? static_cast<unsigned>(ELF::EF_RISCC_RC32)
                          : 0u;
    ES.getWriter().setELFHeaderEFlags(ELF::EF_RISCC_ABI_V0 | Profile | Config);
  }
};
} // namespace

MCStreamer *llvm::createRISCCELFStreamer(const Triple &, MCContext &Context,
                                         std::unique_ptr<MCAsmBackend> &&MAB,
                                         std::unique_ptr<MCObjectWriter> &&MOW,
                                         std::unique_ptr<MCCodeEmitter> &&MCE) {
  return new RISCCELFStreamer(Context, std::move(MAB), std::move(MOW),
                              std::move(MCE));
}

MCTargetStreamer *
llvm::createRISCCObjectTargetStreamer(MCStreamer &S,
                                      const MCSubtargetInfo &STI) {
  if (STI.getTargetTriple().isOSBinFormatELF())
    return new RISCCTargetELFStreamer(S, STI);
  return nullptr;
}
