#include "RISCCFixupKinds.h"
#include "RISCCMCTargetDesc.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
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
    // Determine the encoded size before the base streamer appends it.  The
    // marker is object-only, so it does not pollute llvm-mc -show-encoding.
    SmallString<16> Bytes;
    SmallVector<MCFixup, 4> IgnoredFixups;
    getAssembler().getEmitter().encodeInstruction(Inst, Bytes, IgnoredFixups,
                                                  STI);
    MCELFStreamer::emitInstruction(Inst, STI);
    if (Bytes.empty())
      return;

    MCFragment *F = getCurrentFragment();
    assert(getCurFragSize() >= Bytes.size());
    F->addFixup(MCFixup::create(
        getCurFragSize() - Bytes.size(),
        MCConstantExpr::create(0, getContext()), RISCC::fixup_insn_align));
  }
};

class RISCCTargetELFStreamer final : public MCTargetStreamer {
public:
  RISCCTargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI)
      : MCTargetStreamer(S) {
    auto &ES = static_cast<MCELFStreamer &>(Streamer);
    unsigned Profile = STI.hasFeature(RISCC::FeatureMul)
                           ? ELF::EF_RISCC_PROFILE_FULL
                       : STI.hasFeature(RISCC::FeatureSys) ||
                                 STI.hasFeature(RISCC::FeatureWideShift)
                           ? ELF::EF_RISCC_PROFILE_SYS
                           : ELF::EF_RISCC_PROFILE_MIN;
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
