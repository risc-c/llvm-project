//===-- RISCCELFObjectWriter.cpp - RISCC ELF Object Writer ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

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
      case RISCCMCExpr::VK_LO8:
        return ELF::R_RISCC_LO8;
      case RISCCMCExpr::VK_HI8:
        return ELF::R_RISCC_HI8;
      case RISCCMCExpr::VK_CODE_LO8:
        return ELF::R_RISCC_CODE_LO8;
      case RISCCMCExpr::VK_CODE_HI8:
        return ELF::R_RISCC_CODE_HI8;
      default:
        return ELF::R_RISCC_ABS8;
      }
    case FK_Data_2:
      return Target.getSpecifier() == RISCCMCExpr::VK_CODE
                 ? ELF::R_RISCC_CODE16
                 : ELF::R_RISCC_ABS16;
    case FK_Data_4:
      switch (Target.getSpecifier()) {
      case RISCCMCExpr::VK_TPOFF:
        return ELF::R_RISCC_TPOFF32;
      case RISCCMCExpr::VK_CALL_TARGET:
        return ELF::R_RISCC_CALL_TARGET;
      default:
        return ELF::R_RISCC_ABS32;
      }
    case RISCC::fixup_abs8:
      return ELF::R_RISCC_ABS8;
    case RISCC::fixup_abs16:
      return ELF::R_RISCC_ABS16;
    case RISCC::fixup_abs32:
      return ELF::R_RISCC_ABS32;
    case RISCC::fixup_lo8:
      return ELF::R_RISCC_LO8;
    case RISCC::fixup_hi8:
      return ELF::R_RISCC_HI8;
    case RISCC::fixup_code16:
      return ELF::R_RISCC_CODE16;
    case RISCC::fixup_code_lo8:
      return ELF::R_RISCC_CODE_LO8;
    case RISCC::fixup_code_hi8:
      return ELF::R_RISCC_CODE_HI8;
    case RISCC::fixup_jall21:
      return ELF::R_RISCC_JALL21;
    case RISCC::fixup_pcrel8_word:
    case RISCC::fixup_pcrel8_branch:
      return ELF::R_RISCC_PCREL8_WORD;
    case RISCC::fixup_tpoff_lo8:
      return ELF::R_RISCC_TPOFF_LO8;
    case RISCC::fixup_tpoff_hi8:
      return ELF::R_RISCC_TPOFF_HI8;
    default:
      llvm_unreachable("invalid RISC-C fixup kind");
    }
  }

  bool needsRelocateWithSymbol(const MCValue &, unsigned Type) const override {
    // Prefer retaining the callee symbol for relaxation. Local symbols may
    // still use the canonical section-plus-offset form, which lld adjusts as
    // the section shrinks.
    return Type == ELF::R_RISCC_CALL_TARGET;
  }
};
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createRISCCELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<RISCCELFObjectWriter>(OSABI);
}
