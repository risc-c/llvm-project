//===-- RISCCAsmBackend.cpp - RISCC Assembler Backend ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCFixupKinds.h"
#include "RISCCMCExpr.h"
#include "RISCCMCTargetDesc.h"
#include "llvm/ADT/APInt.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class RISCCAsmBackend final : public MCAsmBackend {
  uint8_t OSABI;

  void checkCodeAddress(const MCFixup &Fixup, uint64_t Value) const {
    if (Value & 1)
      getContext().reportError(Fixup.getLoc(),
                               "code address must be 2-byte aligned");
    if (Value > 0xffff)
      getContext().reportError(Fixup.getLoc(),
                               "code address exceeds 16-bit range");
  }

public:
  explicit RISCCAsmBackend(uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::little), OSABI(OSABI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createRISCCELFObjectWriter(OSABI);
  }

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override {
    if (Name == "R_RISCC_NONE")
      return FirstLiteralRelocationKind + ELF::R_RISCC_NONE;
    if (Name == "R_RISCC_RELAX_CALL")
      return FirstLiteralRelocationKind + ELF::R_RISCC_RELAX_CALL;
    if (Name == "R_RISCC_RELAX_TAIL")
      return FirstLiteralRelocationKind + ELF::R_RISCC_RELAX_TAIL;
    return std::nullopt;
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    static const MCFixupKindInfo Infos[] = {
        {"fixup_abs8", 0, 8, 0},          {"fixup_lo8", 0, 8, 0},
        {"fixup_hi8", 0, 8, 0},           {"fixup_code16", 0, 16, 0},
        {"fixup_code_lo8", 0, 8, 0},      {"fixup_code_hi8", 0, 8, 0},
        {"fixup_jall21", 0, 32, 0},       {"fixup_pcrel8_word", 0, 8, 0},
        {"fixup_pcrel8_branch", 0, 8, 0}, {"fixup_tpoff_lo8", 0, 8, 0},
        {"fixup_tpoff_hi8", 0, 8, 0},     {"fixup_insn_align", 0, 0, 0},
    };
    static_assert(std::size(Infos) == RISCC::NumTargetFixupKinds);
    if (mc::isRelocRelocation(Kind))
      return MCAsmBackend::getFixupKindInfo(FK_NONE);
    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    return Infos[Kind - FirstTargetFixupKind];
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    unsigned Kind = Fixup.getKind();

    if (Kind == FK_Data_8 && !IsResolved) {
      getContext().reportError(Fixup.getLoc(),
                               "RISC-C does not support 64-bit relocations");
      return;
    }

    if (unsigned Specifier = Target.getSpecifier()) {
      bool Valid;
      switch (Specifier) {
      case RISCCMCExpr::VK_LO8:
        Valid = Kind == FK_Data_1 || Kind == RISCC::fixup_lo8;
        break;
      case RISCCMCExpr::VK_HI8:
        Valid = Kind == FK_Data_1 || Kind == RISCC::fixup_hi8;
        break;
      case RISCCMCExpr::VK_CODE_LO8:
        Valid = Kind == FK_Data_1 || Kind == RISCC::fixup_code_lo8;
        break;
      case RISCCMCExpr::VK_CODE_HI8:
        Valid = Kind == FK_Data_1 || Kind == RISCC::fixup_code_hi8;
        break;
      case RISCCMCExpr::VK_CODE:
        Valid = Kind == FK_Data_2 || Kind == RISCC::fixup_code16 ||
                Kind == RISCC::fixup_code_lo8 ||
                Kind == RISCC::fixup_code_hi8 || Kind == RISCC::fixup_jall21;
        break;
      case RISCCMCExpr::VK_TPOFF:
        Valid = Kind == FK_Data_4 || Kind == RISCC::fixup_tpoff_lo8 ||
                Kind == RISCC::fixup_tpoff_hi8;
        break;
      case RISCCMCExpr::VK_CALL_TARGET:
        Valid = Kind == FK_Data_4;
        break;
      default:
        llvm_unreachable("invalid RISC-C expression modifier");
      }
      if (!Valid) {
        getContext().reportError(
            Fixup.getLoc(), "relocation modifier is not valid for this field");
        return;
      }
    }

    // LDPC needs the final absolute address for its word-alignment check.
    // Sys/Full call relaxation can also change local branch displacements.
    // Use the merged object profile, which includes per-function features.
    unsigned Flags =
        static_cast<ELFObjectWriter &>(Asm->getWriter()).getELFHeaderEFlags();
    unsigned Profile = Flags & ELF::EF_RISCC_PROFILE_MASK;
    if (Kind == RISCC::fixup_pcrel8_word ||
        (Kind == RISCC::fixup_pcrel8_branch && (Flags & ELF::EF_RISCC_RC32) &&
         (Profile == ELF::EF_RISCC_PROFILE_SYS ||
          Profile == ELF::EF_RISCC_PROFILE_FULL)))
      IsResolved = false;

    maybeAddReloc(F, Fixup, Target, Value, IsResolved);

    // Marker relocations have no encoded field to update.
    if (mc::isRelocRelocation(Kind))
      return;

    if (Kind == RISCC::fixup_insn_align) {
      uint64_t Address = Asm->getFragmentOffset(F) + Fixup.getOffset();
      if (Address & 1)
        getContext().reportError(Fixup.getLoc(),
                                 "instruction must be 2-byte aligned");
      return;
    }

    if (Kind == RISCC::fixup_jall21) {
      if (!IsResolved)
        return;
      if ((Value & 1) || Value > 0x1fffff) {
        getContext().reportError(Fixup.getLoc(),
                                 "direct target exceeds JALL address range");
        return;
      }
      uint16_t Head = support::endian::read16le(Data);
      Head = (Head & ~0x07c0) | ((Value >> 16) & 0x1f) << 6;
      support::endian::write16le(Data, Head);
      support::endian::write16le(Data + 2, Value);
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
          checkCodeAddress(Fixup, Value);
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
        checkCodeAddress(Fixup, Value);
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
        checkCodeAddress(Fixup, Value);
        if (Kind == RISCC::fixup_code_lo8)
          Value &= 0xff;
        if (Kind == RISCC::fixup_code_hi8)
          Value >>= 8;
        break;
      case RISCC::fixup_pcrel8_word:
      case RISCC::fixup_pcrel8_branch: {
        if (Value & 1)
          getContext().reportError(Fixup.getLoc(),
                                   "branch target must be 2-byte aligned");
        int64_t Rel = (static_cast<int64_t>(Value) - 2) / 2;
        if (!isInt<8>(Rel))
          getContext().reportError(Fixup.getLoc(),
                                   "branch target out of signed 8-bit range");
        uint64_t Encoded = Rel & 0xff;
        Value = ((Encoded << 1) & 0xfe) | (Encoded >> 7);
        break;
      }
      default:
        break;
      }
    } else {
      Value = 0;
    }

    MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());
    assert(Info.TargetOffset == 0 && Info.TargetSize % 8 == 0 &&
           "expected a whole-byte fixup");
    for (unsigned I = 0; I != Info.TargetSize / 8; ++I)
      Data[I] = Value >> (I * 8);
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
} // namespace

MCAsmBackend *llvm::createRISCCMCAsmBackend(const Target &,
                                            const MCSubtargetInfo &,
                                            const MCRegisterInfo &,
                                            const MCTargetOptions &) {
  return new RISCCAsmBackend(ELF::ELFOSABI_STANDALONE);
}
