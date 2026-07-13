#include "RISCCFixupKinds.h"
#include "RISCCMCExpr.h"
#include "RISCCMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

namespace {
class RISCCMCCodeEmitter final : public MCCodeEmitter {
  const MCInstrInfo &MCII;
  MCContext &Ctx;

  unsigned reg(const MCOperand &Op) const {
    return Ctx.getRegisterInfo()->getEncodingValue(Op.getReg()) & 7;
  }
  static uint16_t ri(unsigned Rd, unsigned Op, unsigned Imm) {
    return 0x8000 | (Rd << 11) | (Op << 8) | (Imm & 0xff);
  }
  static uint16_t rr(unsigned Rd, unsigned Ra, unsigned Func, unsigned Rb) {
    return 0xc000 | (Rd << 11) | (Ra << 8) | (Func << 3) | Rb;
  }
  static void emit16(SmallVectorImpl<char> &CB, uint16_t Word) {
    support::endian::write(CB, Word, llvm::endianness::little);
  }
  unsigned immediate(const MCOperand &Op, SmallVectorImpl<MCFixup> &Fixups,
                     unsigned Offset, RISCC::Fixups DefaultKind) const;
  unsigned branchImmediate(const MCOperand &Op,
                           SmallVectorImpl<MCFixup> &Fixups) const;
  unsigned codeImmediate(const MCOperand &Op, SmallVectorImpl<MCFixup> &Fixups,
                         unsigned Offset, SMLoc Loc) const;

public:
  RISCCMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx)
      : MCII(MCII), Ctx(Ctx) {}
  void encodeInstruction(const MCInst &, SmallVectorImpl<char> &,
                         SmallVectorImpl<MCFixup> &,
                         const MCSubtargetInfo &) const override;
};
}

unsigned RISCCMCCodeEmitter::branchImmediate(
    const MCOperand &Op, SmallVectorImpl<MCFixup> &Fixups) const {
  if (Op.isImm()) {
    if (!isInt<8>(Op.getImm())) {
      Ctx.reportError(SMLoc(), "branch displacement exceeds signed 8-bit range");
      return 0;
    }
    return Op.getImm() & 0xff;
  }
  const MCExpr *Expr = Op.getExpr();
  if (const auto *RE = dyn_cast<RISCCMCExpr>(Expr)) {
    Ctx.reportError(SMLoc(), "target modifier is invalid on a short branch");
    Expr = RE->getSubExpr();
  }
  Fixups.push_back(MCFixup::create(0, Expr, RISCC::fixup_pcrel8_word, true));
  return 0;
}

unsigned RISCCMCCodeEmitter::codeImmediate(
    const MCOperand &Op, SmallVectorImpl<MCFixup> &Fixups, unsigned Offset,
    SMLoc Loc) const {
  if (Op.isImm()) {
    int64_t V = Op.getImm();
    if ((V & 1) || V < 0 || V > 0xfffe) {
      Ctx.reportError(Loc, "direct target is not a 15-bit word address");
      return 0;
    }
    return V >> 1;
  }
  const MCExpr *Expr = Op.getExpr();
  if (const auto *RE = dyn_cast<RISCCMCExpr>(Expr)) {
    if (RE->getKind() != RISCCMCExpr::VK_CODE)
      Ctx.reportError(Loc, "only code() is valid on a direct control target");
    Expr = RE->getSubExpr();
  }
  Fixups.push_back(MCFixup::create(Offset, Expr, RISCC::fixup_code16));
  return 0;
}

unsigned RISCCMCCodeEmitter::immediate(const MCOperand &Op,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       unsigned Offset,
                                       RISCC::Fixups DefaultKind) const {
  if (Op.isImm())
    return Op.getImm();

  const MCExpr *Expr = Op.getExpr();
  RISCC::Fixups Kind = DefaultKind;
  if (const auto *RE = dyn_cast<RISCCMCExpr>(Expr)) {
    switch (RE->getKind()) {
    case RISCCMCExpr::VK_None: break;
    case RISCCMCExpr::VK_LO8: Kind = RISCC::fixup_lo8; break;
    case RISCCMCExpr::VK_HI8: Kind = RISCC::fixup_hi8; break;
    case RISCCMCExpr::VK_CODE:
      Kind = DefaultKind == RISCC::fixup_hi8 ? RISCC::fixup_code_hi8
           : DefaultKind == RISCC::fixup_lo8 ? RISCC::fixup_code_lo8
                                             : RISCC::fixup_code16;
      break;
    case RISCCMCExpr::VK_CODE_LO8: Kind = RISCC::fixup_code_lo8; break;
    case RISCCMCExpr::VK_CODE_HI8: Kind = RISCC::fixup_code_hi8; break;
    case RISCCMCExpr::VK_TPOFF:
      if (DefaultKind == RISCC::fixup_lo8)
        Kind = RISCC::fixup_tpoff_lo8;
      else if (DefaultKind == RISCC::fixup_hi8)
        Kind = RISCC::fixup_tpoff_hi8;
      else
        Ctx.reportError(SMLoc(), "tpoff() requires a 16-bit immediate");
      break;
    }
    Expr = RE->getSubExpr();
  }
  bool PCRel = Kind == RISCC::fixup_pcrel8_word;
  Fixups.push_back(MCFixup::create(Offset, Expr, Kind, PCRel));
  return 0;
}

void RISCCMCCodeEmitter::encodeInstruction(
    const MCInst &MI, SmallVectorImpl<char> &CB,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &) const {
  unsigned O = MI.getOpcode();
  uint16_t W = 0;

  switch (O) {
  case RISCC::LDW:
  case RISCC::STW: {
    unsigned Major = O == RISCC::STW;
    W = (Major << 14) | (reg(MI.getOperand(0)) << 11) |
        (reg(MI.getOperand(1)) << 8) |
        (immediate(MI.getOperand(2), Fixups, 0, RISCC::fixup_abs8) & 0xff);
    break;
  }
  case RISCC::LDI:
  case RISCC::LUI:
  case RISCC::CMPI: {
    unsigned IOp = O == RISCC::LDI ? 0 : O == RISCC::LUI ? 1 : 3;
    W = ri(reg(MI.getOperand(0)), IOp,
           immediate(MI.getOperand(1), Fixups, 0,
                     O == RISCC::LUI ? RISCC::fixup_hi8
                                     : RISCC::fixup_lo8));
    break;
  }
  case RISCC::ADDI:
  case RISCC::ANDI:
  case RISCC::ORI:
  case RISCC::XORI: {
    unsigned IOp = O == RISCC::ADDI ? 2 : O == RISCC::ANDI ? 4
                                     : O == RISCC::ORI ? 5 : 6;
    unsigned ImmOp = MI.getNumOperands() == 3 ? 2 : 1;
    W = ri(reg(MI.getOperand(0)), IOp,
           immediate(MI.getOperand(ImmOp), Fixups, 0, RISCC::fixup_lo8));
    break;
  }
  case RISCC::BEQZ:
  case RISCC::BNEZ:
  case RISCC::BLTZ:
  case RISCC::BGEZ:
  case RISCC::JMP8: {
    unsigned CC = O == RISCC::BEQZ ? 0 : O == RISCC::BNEZ ? 1
                                : O == RISCC::BLTZ ? 2 : O == RISCC::BGEZ ? 3 : 4;
    W = ri(CC, 7, branchImmediate(MI.getOperand(0), Fixups));
    break;
  }
  case RISCC::ADD: case RISCC::SUB: case RISCC::SLT: case RISCC::SLTU:
  case RISCC::AND: case RISCC::OR: case RISCC::XOR: case RISCC::MUL:
  case RISCC::LDWX: case RISCC::LDB: case RISCC::LDBS: {
    unsigned Func = O == RISCC::ADD ? 0 : O == RISCC::SUB ? 1
      : O == RISCC::SLT ? 2 : O == RISCC::SLTU ? 3 : O == RISCC::AND ? 4
      : O == RISCC::OR ? 5 : O == RISCC::XOR ? 6 : O == RISCC::MUL ? 7
      : O == RISCC::LDWX ? 8 : O == RISCC::LDB ? 10 : 14;
    W = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), Func,
           reg(MI.getOperand(2)));
    break;
  }
  case RISCC::STB:
    W = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), 0x0b, 0);
    break;
  case RISCC::SHRI: case RISCC::SARI: case RISCC::SHLI: {
    unsigned Func = O == RISCC::SHRI ? 0x0c : O == RISCC::SARI ? 0x0d : 0x0f;
    W = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), Func,
           (MI.getOperand(2).getImm() - 1) & 7);
    break;
  }
  case RISCC::RET: case RISCC::RETI:
    W = rr(O == RISCC::RETI ? 7 : 0, reg(MI.getOperand(0)), 0x1f, 0);
    break;
  case RISCC::JAL:
    W = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), 0x1f, 1);
    break;
  case RISCC::MFS: case RISCC::MTS:
    W = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), 0x1f,
           O == RISCC::MFS ? 2 : 3);
    break;
  case RISCC::CLI: case RISCC::STI:
    W = rr(O == RISCC::STI ? 7 : 0, 0, 0x1f, 6);
    break;
  case RISCC::JAL16:
    W = rr(reg(MI.getOperand(0)), 0, 0x1f, 5);
    emit16(CB, W);
    emit16(CB, codeImmediate(MI.getOperand(1), Fixups, 2, MI.getLoc()));
    return;
  case RISCC::CALL16:
  case RISCC::JMP16:
    W = rr(O == RISCC::CALL16 ? 7 : 0, 0, 0x1f, 5);
    emit16(CB, W);
    emit16(CB, codeImmediate(MI.getOperand(0), Fixups, 2, MI.getLoc()));
    return;
  case RISCC::CALL:
    W = rr(7, reg(MI.getOperand(0)), 0x1f, 1);
    break;
  case RISCC::RETS:
    W = rr(0, 7, 0x1f, 0);
    break;
  case RISCC::MOV:
    W = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), 5,
           reg(MI.getOperand(1)));
    break;
  case RISCC::NOP: W = rr(0, 0, 5, 0); break;
  case RISCC::HALT: W = ri(4, 7, 0xff); break;
  case RISCC::LONG_BR: {
    unsigned ShortOpc = MI.getOperand(0).getImm();
    unsigned InvCC = ShortOpc == RISCC::BEQZ ? 1
                   : ShortOpc == RISCC::BNEZ ? 0
                   : ShortOpc == RISCC::BLTZ ? 3 : 2;
    emit16(CB, ri(InvCC, 7, 2));
    emit16(CB, rr(0, 0, 0x1f, 5));
    emit16(CB, codeImmediate(MI.getOperand(1), Fixups, 4, MI.getLoc()));
    return;
  }
  case RISCC::LI: {
    unsigned Rd = reg(MI.getOperand(0));
    const MCOperand &Imm = MI.getOperand(1);
    if (Imm.isImm()) {
      unsigned V = Imm.getImm();
      emit16(CB, ri(Rd, 1, V >> 8));
      emit16(CB, ri(Rd, 5, V));
    } else {
      const MCExpr *Expr = Imm.getExpr();
      RISCCMCExpr::VariantKind Variant = RISCCMCExpr::VK_None;
      if (const auto *RE = dyn_cast<RISCCMCExpr>(Expr)) {
        Variant = RE->getKind();
        if (Variant != RISCCMCExpr::VK_CODE &&
            Variant != RISCCMCExpr::VK_TPOFF)
          Ctx.reportError(MI.getLoc(),
                          "LI accepts only an unmodified, code(), or tpoff() expression");
        Expr = RE->getSubExpr();
      }
      RISCC::Fixups Hi = Variant == RISCCMCExpr::VK_CODE
                             ? RISCC::fixup_code_hi8
                         : Variant == RISCCMCExpr::VK_TPOFF
                             ? RISCC::fixup_tpoff_hi8
                             : RISCC::fixup_hi8;
      RISCC::Fixups Lo = Variant == RISCCMCExpr::VK_CODE
                             ? RISCC::fixup_code_lo8
                         : Variant == RISCCMCExpr::VK_TPOFF
                             ? RISCC::fixup_tpoff_lo8
                             : RISCC::fixup_lo8;
      Fixups.push_back(MCFixup::create(
          0, Expr, Hi));
      Fixups.push_back(MCFixup::create(
          2, Expr, Lo));
      emit16(CB, ri(Rd, 1, 0));
      emit16(CB, ri(Rd, 5, 0));
    }
    return;
  }
  default:
    Ctx.reportError(MI.getLoc(), "unsupported RISC-C instruction encoding");
    return;
  }
  emit16(CB, W);
}

MCCodeEmitter *llvm::createRISCCMCCodeEmitter(const MCInstrInfo &MCII,
                                               MCContext &Ctx) {
  return new RISCCMCCodeEmitter(MCII, Ctx);
}
