//===-- RISCCMCCodeEmitter.cpp - RISCC Code Emitter -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCFixupKinds.h"
#include "RISCCMCExpr.h"
#include "RISCCMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class RISCCMCCodeEmitter final : public MCCodeEmitter {
  MCContext &Ctx;
  const MCInstrInfo &MCII;

  static void emit16(SmallVectorImpl<char> &Code, uint16_t Word) {
    support::endian::write(Code, Word, llvm::endianness::little);
  }

  unsigned immediate(const MCOperand &Op, SmallVectorImpl<MCFixup> &Fixups,
                     unsigned Offset, RISCC::Fixups DefaultKind,
                     SMLoc Loc) const;
  unsigned branchImmediate(const MCOperand &Op,
                           SmallVectorImpl<MCFixup> &Fixups, SMLoc Loc) const;
  unsigned codeImmediate(const MCOperand &Op, SmallVectorImpl<MCFixup> &Fixups,
                         unsigned Offset, SMLoc Loc) const;
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
  uint64_t getMachineOpValue(const MCInst &MI, const MCOperand &Op,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;
  uint64_t getShiftAmountEncoding(const MCInst &MI, unsigned OpNo,
                                  SmallVectorImpl<MCFixup> &Fixups,
                                  const MCSubtargetInfo &STI) const;
  uint64_t getBranchTargetEncoding(const MCInst &MI, unsigned OpNo,
                                   SmallVectorImpl<MCFixup> &Fixups,
                                   const MCSubtargetInfo &STI) const;
  uint64_t getCodeTargetEncoding(const MCInst &MI, unsigned OpNo,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

public:
  RISCCMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx)
      : Ctx(Ctx), MCII(MCII) {}

  void encodeInstruction(const MCInst &, SmallVectorImpl<char> &,
                         SmallVectorImpl<MCFixup> &,
                         const MCSubtargetInfo &) const override;
};
} // namespace

unsigned RISCCMCCodeEmitter::branchImmediate(
    const MCOperand &Op, SmallVectorImpl<MCFixup> &Fixups, SMLoc Loc) const {
  if (Op.isImm()) {
    if (!isInt<8>(Op.getImm())) {
      Ctx.reportError(Loc, "branch displacement exceeds signed 8-bit range");
      return 0;
    }
    return Op.getImm() & 0xff;
  }
  const MCExpr *Expr = Op.getExpr();
  if (const auto *TargetExpr = dyn_cast<RISCCMCExpr>(Expr)) {
    Ctx.reportError(Loc, "target modifier is invalid on a short branch");
    Expr = TargetExpr->getSubExpr();
  }
  Fixups.push_back(MCFixup::create(0, Expr, RISCC::fixup_pcrel8_word, true));
  return 0;
}

unsigned RISCCMCCodeEmitter::codeImmediate(
    const MCOperand &Op, SmallVectorImpl<MCFixup> &Fixups, unsigned Offset,
    SMLoc Loc) const {
  if (Op.isImm()) {
    int64_t Value = Op.getImm();
    if ((Value & 1) || Value < 0 || Value > 0xfffe) {
      Ctx.reportError(Loc, "direct target is not a 15-bit word address");
      return 0;
    }
    return Value >> 1;
  }
  const MCExpr *Expr = Op.getExpr();
  if (const auto *TargetExpr = dyn_cast<RISCCMCExpr>(Expr)) {
    if (TargetExpr->getKind() != RISCCMCExpr::VK_CODE)
      Ctx.reportError(Loc, "only code() is valid on a direct control target");
    Expr = TargetExpr->getSubExpr();
  }
  Fixups.push_back(MCFixup::create(Offset, Expr, RISCC::fixup_code16));
  return 0;
}

unsigned RISCCMCCodeEmitter::immediate(const MCOperand &Op,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       unsigned Offset,
                                       RISCC::Fixups DefaultKind,
                                       SMLoc Loc) const {
  if (Op.isImm())
    return Op.getImm();

  const MCExpr *Expr = Op.getExpr();
  RISCC::Fixups Kind = DefaultKind;
  if (const auto *TargetExpr = dyn_cast<RISCCMCExpr>(Expr)) {
    switch (TargetExpr->getKind()) {
    case RISCCMCExpr::VK_None:
      break;
    case RISCCMCExpr::VK_LO8:
      Kind = RISCC::fixup_lo8;
      break;
    case RISCCMCExpr::VK_HI8:
      Kind = RISCC::fixup_hi8;
      break;
    case RISCCMCExpr::VK_CODE:
      if (DefaultKind == RISCC::fixup_hi8)
        Kind = RISCC::fixup_code_hi8;
      else if (DefaultKind == RISCC::fixup_lo8)
        Kind = RISCC::fixup_code_lo8;
      else
        Kind = RISCC::fixup_code16;
      break;
    case RISCCMCExpr::VK_CODE_LO8:
      Kind = RISCC::fixup_code_lo8;
      break;
    case RISCCMCExpr::VK_CODE_HI8:
      Kind = RISCC::fixup_code_hi8;
      break;
    case RISCCMCExpr::VK_TPOFF:
      if (DefaultKind == RISCC::fixup_lo8)
        Kind = RISCC::fixup_tpoff_lo8;
      else if (DefaultKind == RISCC::fixup_hi8)
        Kind = RISCC::fixup_tpoff_hi8;
      else
        Ctx.reportError(Loc, "tpoff() requires a 16-bit immediate");
      break;
    }
    Expr = TargetExpr->getSubExpr();
  }
  Fixups.push_back(MCFixup::create(
      Offset, Expr, Kind, Kind == RISCC::fixup_pcrel8_word));
  return 0;
}

uint64_t RISCCMCCodeEmitter::getMachineOpValue(
    const MCInst &MI, const MCOperand &Op, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &) const {
  if (Op.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(Op.getReg()) & 7;
  if (Op.isImm())
    return Op.getImm();

  RISCC::Fixups Kind = RISCC::fixup_lo8;
  if (MI.getOpcode() == RISCC::LUI)
    Kind = RISCC::fixup_hi8;
  else if (MI.getOpcode() == RISCC::LDW || MI.getOpcode() == RISCC::STW ||
           MI.getOpcode() == RISCC::LDW_NANO ||
           MI.getOpcode() == RISCC::STW_NANO)
    Kind = RISCC::fixup_abs8;
  return immediate(Op, Fixups, 0, Kind, MI.getLoc());
}

uint64_t RISCCMCCodeEmitter::getShiftAmountEncoding(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &,
    const MCSubtargetInfo &) const {
  return MI.getOperand(OpNo).getImm() - 1;
}

uint64_t RISCCMCCodeEmitter::getBranchTargetEncoding(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &) const {
  return branchImmediate(MI.getOperand(OpNo), Fixups, MI.getLoc());
}

uint64_t RISCCMCCodeEmitter::getCodeTargetEncoding(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &) const {
  return codeImmediate(MI.getOperand(OpNo), Fixups, 2, MI.getLoc());
}

void RISCCMCCodeEmitter::encodeInstruction(
    const MCInst &MI, SmallVectorImpl<char> &Code,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  const unsigned Opcode = MI.getOpcode();
  auto Encode = [&](MCInst Expanded) {
    Expanded.setLoc(MI.getLoc());
    encodeInstruction(Expanded, Code, Fixups, STI);
  };
  switch (Opcode) {
  case RISCC::CALL16:
  case RISCC::JMP16:
  case RISCC::TAIL16: {
    Encode(MCInstBuilder(RISCC::JAL16)
               .addReg(Opcode == RISCC::CALL16 ? RISCC::S7 : RISCC::S0)
               .addOperand(MI.getOperand(0)));
    return;
  }
  case RISCC::CALL:
  case RISCC::TAIL_REG: {
    Encode(MCInstBuilder(RISCC::JALR)
               .addReg(Opcode == RISCC::CALL ? RISCC::S7 : RISCC::S0)
               .addOperand(MI.getOperand(0)));
    return;
  }
  case RISCC::CALL_NANO_REG:
  case RISCC::TAIL_NANO_REG: {
    Encode(MCInstBuilder(RISCC::JALR_NANO)
               .addReg(Opcode == RISCC::CALL_NANO_REG ? RISCC::R6 : RISCC::R0)
               .addOperand(MI.getOperand(0)));
    return;
  }
  case RISCC::RETS: {
    Encode(MCInstBuilder(RISCC::RET).addReg(RISCC::S7));
    return;
  }
  case RISCC::RET_NANO: {
    Encode(MCInstBuilder(RISCC::JALR_NANO)
               .addReg(RISCC::R0)
               .addOperand(MI.getOperand(0)));
    return;
  }
  case RISCC::MOV: {
    Encode(MCInstBuilder(RISCC::OR)
               .addOperand(MI.getOperand(0))
               .addOperand(MI.getOperand(1))
               .addOperand(MI.getOperand(1)));
    return;
  }
  case RISCC::NOP: {
    Encode(MCInstBuilder(RISCC::OR)
               .addReg(RISCC::R0)
               .addReg(RISCC::R0)
               .addReg(RISCC::R0));
    return;
  }
  case RISCC::HALT: {
    Encode(MCInstBuilder(RISCC::JMP8).addImm(-1));
    return;
  }
  case RISCC::LDI16:
  case RISCC::LI: {
    const MCOperand &Imm = MI.getOperand(1);
    if (Imm.isExpr()) {
      if (const auto *TargetExpr = dyn_cast<RISCCMCExpr>(Imm.getExpr())) {
        RISCCMCExpr::VariantKind Variant = TargetExpr->getKind();
        if (Variant != RISCCMCExpr::VK_None &&
            Variant != RISCCMCExpr::VK_CODE &&
            Variant != RISCCMCExpr::VK_TPOFF) {
          Ctx.reportError(
              MI.getLoc(),
              "LI accepts only an unmodified, code(), or tpoff() expression");
          return;
        }
      }
    }
    MCOperand Hi, Lo;
    if (Imm.isImm()) {
      const unsigned Value = Imm.getImm();
      Hi = MCOperand::createImm(Value >> 8);
      Lo = MCOperand::createImm(Value & 0xff);
    } else {
      const MCExpr *Expr = Imm.getExpr();
      if (const auto *TargetExpr = dyn_cast<RISCCMCExpr>(Expr)) {
        RISCCMCExpr::VariantKind Variant = TargetExpr->getKind();
        if (Variant != RISCCMCExpr::VK_CODE &&
            Variant != RISCCMCExpr::VK_TPOFF)
          Ctx.reportError(
              MI.getLoc(),
              "LI accepts only an unmodified, code(), or tpoff() expression");
      }
      Hi = Lo = MCOperand::createExpr(Expr);
    }

    Encode(MCInstBuilder(RISCC::LUI)
               .addOperand(MI.getOperand(0))
               .addOperand(Hi));

    unsigned FirstLowFixup = Fixups.size();
    Encode(MCInstBuilder(RISCC::ORI)
               .addOperand(MI.getOperand(0))
               .addOperand(MI.getOperand(0))
               .addOperand(Lo));
    for (unsigned I = FirstLowFixup; I != Fixups.size(); ++I)
      Fixups[I].setOffset(Fixups[I].getOffset() + 2);
    return;
  }
  default:
    break;
  }

  uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
  unsigned Size = MCII.get(Opcode).getSize();
  if (Size != 2 && Size != 4) {
    Ctx.reportError(MI.getLoc(), "unsupported RISC-C instruction encoding");
    return;
  }
  emit16(Code, Bits);
  if (Size == 4)
    emit16(Code, Bits >> 16);
}

MCCodeEmitter *llvm::createRISCCMCCodeEmitter(const MCInstrInfo &MCII,
                                               MCContext &Ctx) {
  return new RISCCMCCodeEmitter(MCII, Ctx);
}

#include "RISCCGenMCCodeEmitter.inc"
