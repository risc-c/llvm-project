#include "RISCCFixupKinds.h"
#include "RISCCMCTargetDesc.h"
#include "RISCCMCExpr.h"
#include "llvm/ADT/APInt.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class RISCCAsmBackend final : public MCAsmBackend {
  uint8_t OSABI;

  void adjustCodeAddress(const MCFixup &Fixup, uint64_t &Value) const {
    if (Value & 1)
      getContext().reportError(Fixup.getLoc(),
                               "code address must be 2-byte aligned");
    Value >>= 1;
    if (Value > 0x7fff)
      getContext().reportError(Fixup.getLoc(),
                               "code address exceeds 15-bit range");
  }

public:
  explicit RISCCAsmBackend(uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::little), OSABI(OSABI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createRISCCELFObjectWriter(OSABI);
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    static const MCFixupKindInfo Infos[] = {
        {"fixup_abs8", 0, 8, 0},
        {"fixup_abs16", 0, 16, 0},
        {"fixup_abs32", 0, 32, 0},
        {"fixup_lo8", 0, 8, 0},
        {"fixup_hi8", 0, 8, 0},
        {"fixup_code16", 0, 16, 0},
        {"fixup_code_lo8", 0, 8, 0},
        {"fixup_code_hi8", 0, 8, 0},
        {"fixup_pcrel8_word", 0, 8, 0},
        {"fixup_tpoff_lo8", 0, 8, 0},
        {"fixup_tpoff_hi8", 0, 8, 0},
        {"fixup_insn_align", 0, 0, 0},
    };
    static_assert(std::size(Infos) == RISCC::NumTargetFixupKinds);
    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    return Infos[Kind - FirstTargetFixupKind];
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    maybeAddReloc(F, Fixup, Target, Value, IsResolved);
    unsigned Kind = Fixup.getKind();

    if (Kind == RISCC::fixup_insn_align) {
      uint64_t Address = Asm->getFragmentOffset(F) + Fixup.getOffset();
      if (Address & 1)
        getContext().reportError(Fixup.getLoc(),
                                 "instruction must be 2-byte aligned");
      return;
    }

    if (IsResolved) {
      if (Kind == FK_Data_1) {
        switch (Target.getSpecifier()) {
        case RISCCMCExpr::VK_LO8:
          Value &= 0xff;
          break;
        case RISCCMCExpr::VK_HI8:
          Value >>= 8;
          break;
        case RISCCMCExpr::VK_CODE_LO8:
        case RISCCMCExpr::VK_CODE_HI8:
          adjustCodeAddress(Fixup, Value);
          if (Target.getSpecifier() == RISCCMCExpr::VK_CODE_LO8)
            Value &= 0xff;
          else
            Value >>= 8;
          break;
        default:
          break;
        }
      }
      if (Kind == FK_Data_2 && Target.getSpecifier() == RISCCMCExpr::VK_CODE) {
        adjustCodeAddress(Fixup, Value);
      }
      switch (Kind) {
      case RISCC::fixup_hi8:
      case RISCC::fixup_tpoff_hi8:
        Value >>= 8;
        break;
      case RISCC::fixup_lo8:
      case RISCC::fixup_tpoff_lo8:
        Value &= 0xff;
        break;
      case RISCC::fixup_code16:
      case RISCC::fixup_code_lo8:
      case RISCC::fixup_code_hi8:
        adjustCodeAddress(Fixup, Value);
        if (Kind == RISCC::fixup_code_lo8)
          Value &= 0xff;
        if (Kind == RISCC::fixup_code_hi8)
          Value >>= 8;
        break;
      case RISCC::fixup_pcrel8_word: {
        if (Value & 1)
          getContext().reportError(Fixup.getLoc(),
                                   "branch target must be 2-byte aligned");
        int64_t Rel = (static_cast<int64_t>(Value) - 2) / 2;
        if (!isInt<8>(Rel))
          getContext().reportError(Fixup.getLoc(),
                                   "branch target out of signed 8-bit range");
        Value = Rel & 0xff;
        break;
      }
      default:
        break;
      }
    } else {
      Value = 0;
    }

    MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());
    unsigned Bytes = alignTo(Info.TargetOffset + Info.TargetSize, 8) / 8;
    // MCAssembler passes Data already advanced to the fixup's location in the
    // fragment.  Applying the fragment-relative offset again corrupts the
    // following fixup (and, for a tail fixup, memory beyond the fragment).
    for (unsigned I = 0; I != Bytes; ++I) {
      unsigned Mask = I + 1 == Bytes && Info.TargetSize % 8
                          ? (1u << (Info.TargetSize % 8)) - 1 : 0xff;
      Data[I] = (Data[I] & ~Mask) | ((Value >> (I * 8)) & Mask);
    }
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *) const override {
    if (Count & 1)
      return false;
    while (Count) {
      support::endian::write<uint16_t>(OS, 0xc028, llvm::endianness::little);
      Count -= 2;
    }
    return true;
  }
};
}

MCAsmBackend *llvm::createRISCCMCAsmBackend(const Target &,
                                             const MCSubtargetInfo &,
                                             const MCRegisterInfo &,
                                             const MCTargetOptions &) {
  return new RISCCAsmBackend(ELF::ELFOSABI_STANDALONE);
}
