#include "RISCCFixupKinds.h"
#include "RISCCMCTargetDesc.h"
#include "RISCCMCExpr.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class RISCCELFObjectWriter final : public MCELFObjectTargetWriter {
public:
  explicit RISCCELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(false, OSABI, ELF::EM_RISCC, true) {}

  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool) const override {
    switch (Fixup.getKind()) {
    case FK_Data_1:
      switch (Target.getSpecifier()) {
      case RISCCMCExpr::VK_LO8: return ELF::R_RISCC_LO8;
      case RISCCMCExpr::VK_HI8: return ELF::R_RISCC_HI8;
      case RISCCMCExpr::VK_CODE_LO8: return ELF::R_RISCC_CODE_LO8;
      case RISCCMCExpr::VK_CODE_HI8: return ELF::R_RISCC_CODE_HI8;
      default: return ELF::R_RISCC_ABS8;
      }
    case FK_Data_2:
      return Target.getSpecifier() == RISCCMCExpr::VK_CODE
                 ? ELF::R_RISCC_CODE16 : ELF::R_RISCC_ABS16;
    case FK_Data_4: return ELF::R_RISCC_ABS32;
    case RISCC::fixup_abs8: return ELF::R_RISCC_ABS8;
    case RISCC::fixup_abs16: return ELF::R_RISCC_ABS16;
    case RISCC::fixup_abs32: return ELF::R_RISCC_ABS32;
    case RISCC::fixup_lo8: return ELF::R_RISCC_LO8;
    case RISCC::fixup_hi8: return ELF::R_RISCC_HI8;
    case RISCC::fixup_code16: return ELF::R_RISCC_CODE16;
    case RISCC::fixup_code_lo8: return ELF::R_RISCC_CODE_LO8;
    case RISCC::fixup_code_hi8: return ELF::R_RISCC_CODE_HI8;
    case RISCC::fixup_pcrel8_word: return ELF::R_RISCC_PCREL8_WORD;
    case RISCC::fixup_tpoff_lo8: return ELF::R_RISCC_TPOFF_LO8;
    case RISCC::fixup_tpoff_hi8: return ELF::R_RISCC_TPOFF_HI8;
    default: llvm_unreachable("invalid RISC-C fixup kind");
    }
  }
};
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createRISCCELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<RISCCELFObjectWriter>(OSABI);
}
