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
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
static unsigned immediateOpcode(unsigned Opcode) {
  switch (Opcode) {
  case RISCC::LDI:
    return 0;
  case RISCC::LUI:
    return 1;
  case RISCC::ADDI:
    return 2;
  case RISCC::CMPI:
    return 3;
  case RISCC::ANDI:
    return 4;
  case RISCC::ORI:
    return 5;
  case RISCC::XORI:
    return 6;
  default:
    llvm_unreachable("not an immediate RISC-C opcode");
  }
}

static unsigned branchCondition(unsigned Opcode) {
  switch (Opcode) {
  case RISCC::BEQZ:
    return 0;
  case RISCC::BNEZ:
    return 1;
  case RISCC::BLTZ:
    return 2;
  case RISCC::BGEZ:
    return 3;
  case RISCC::JMP8:
    return 4;
  default:
    llvm_unreachable("not a short RISC-C branch");
  }
}

static unsigned registerFunction(unsigned Opcode) {
  switch (Opcode) {
  case RISCC::ADD:
    return 0;
  case RISCC::SUB:
    return 1;
  case RISCC::SLT:
    return 2;
  case RISCC::SLTU:
    return 3;
  case RISCC::AND:
    return 4;
  case RISCC::OR:
    return 5;
  case RISCC::XOR:
    return 6;
  case RISCC::MUL:
    return 7;
  case RISCC::LDWX:
    return 8;
  case RISCC::LDB:
    return 10;
  case RISCC::LDBS:
    return 14;
  default:
    llvm_unreachable("not a register RISC-C opcode");
  }
}

class RISCCMCCodeEmitter final : public MCCodeEmitter {
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
                     unsigned Offset, RISCC::Fixups DefaultKind,
                     SMLoc Loc) const;
  unsigned branchImmediate(const MCOperand &Op,
                           SmallVectorImpl<MCFixup> &Fixups, SMLoc Loc) const;
  unsigned codeImmediate(const MCOperand &Op, SmallVectorImpl<MCFixup> &Fixups,
                         unsigned Offset, SMLoc Loc) const;

public:
  explicit RISCCMCCodeEmitter(MCContext &Ctx) : Ctx(Ctx) {}
  void encodeInstruction(const MCInst &, SmallVectorImpl<char> &,
                         SmallVectorImpl<MCFixup> &,
                         const MCSubtargetInfo &) const override;
};
}

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
  if (const auto *RE = dyn_cast<RISCCMCExpr>(Expr)) {
    Ctx.reportError(Loc, "target modifier is invalid on a short branch");
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
                                       RISCC::Fixups DefaultKind,
                                       SMLoc Loc) const {
  if (Op.isImm())
    return Op.getImm();

  const MCExpr *Expr = Op.getExpr();
  RISCC::Fixups Kind = DefaultKind;
  if (const auto *RE = dyn_cast<RISCCMCExpr>(Expr)) {
    switch (RE->getKind()) {
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
    Expr = RE->getSubExpr();
  }
  Fixups.push_back(MCFixup::create(
      Offset, Expr, Kind, Kind == RISCC::fixup_pcrel8_word));
  return 0;
}

void RISCCMCCodeEmitter::encodeInstruction(
    const MCInst &MI, SmallVectorImpl<char> &Code,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &) const {
  const unsigned Opcode = MI.getOpcode();
  uint16_t Word = 0;

  switch (Opcode) {
  case RISCC::LDW:
  case RISCC::STW: {
    const unsigned Major = Opcode == RISCC::STW;
    Word = (Major << 14) | (reg(MI.getOperand(0)) << 11) |
           (reg(MI.getOperand(1)) << 8) |
           (immediate(MI.getOperand(2), Fixups, 0, RISCC::fixup_abs8,
                      MI.getLoc()) &
            0xff);
    break;
  }
  case RISCC::LDI:
  case RISCC::LUI:
  case RISCC::CMPI: {
    const RISCC::Fixups FixupKind =
        Opcode == RISCC::LUI ? RISCC::fixup_hi8 : RISCC::fixup_lo8;
    Word = ri(reg(MI.getOperand(0)), immediateOpcode(Opcode),
              immediate(MI.getOperand(1), Fixups, 0, FixupKind, MI.getLoc()));
    break;
  }
  case RISCC::ADDI:
  case RISCC::ANDI:
  case RISCC::ORI:
  case RISCC::XORI: {
    const unsigned ImmediateOperand = MI.getNumOperands() == 3 ? 2 : 1;
    Word = ri(reg(MI.getOperand(0)), immediateOpcode(Opcode),
              immediate(MI.getOperand(ImmediateOperand), Fixups, 0,
                        RISCC::fixup_lo8, MI.getLoc()));
    break;
  }
  case RISCC::BEQZ:
  case RISCC::BNEZ:
  case RISCC::BLTZ:
  case RISCC::BGEZ:
  case RISCC::JMP8: {
    Word = ri(branchCondition(Opcode), 7,
              branchImmediate(MI.getOperand(0), Fixups, MI.getLoc()));
    break;
  }
  case RISCC::ADD:
  case RISCC::SUB:
  case RISCC::SLT:
  case RISCC::SLTU:
  case RISCC::AND:
  case RISCC::OR:
  case RISCC::XOR:
  case RISCC::MUL:
  case RISCC::LDWX:
  case RISCC::LDB:
  case RISCC::LDBS: {
    Word = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)),
              registerFunction(Opcode),
              reg(MI.getOperand(2)));
    break;
  }
  case RISCC::STB:
    Word = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), 0x0b, 0);
    break;
  case RISCC::SHRI:
  case RISCC::SARI:
  case RISCC::SHLI: {
    const unsigned Function = Opcode == RISCC::SHRI   ? 0x0c
                              : Opcode == RISCC::SARI ? 0x0d
                                                      : 0x0f;
    Word = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), Function,
              (MI.getOperand(2).getImm() - 1) & 7);
    break;
  }
  case RISCC::RET:
  case RISCC::RETI:
    Word = rr(Opcode == RISCC::RETI ? 7 : 0, reg(MI.getOperand(0)), 0x1f, 0);
    break;
  case RISCC::JAL:
    Word = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), 0x1f, 1);
    break;
  case RISCC::MFS:
  case RISCC::MTS:
    Word = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), 0x1f,
              Opcode == RISCC::MFS ? 2 : 3);
    break;
  case RISCC::CLI:
  case RISCC::STI:
    Word = rr(Opcode == RISCC::STI ? 7 : 0, 0, 0x1f, 6);
    break;
  case RISCC::JAL16:
    Word = rr(reg(MI.getOperand(0)), 0, 0x1f, 5);
    emit16(Code, Word);
    emit16(Code,
           codeImmediate(MI.getOperand(1), Fixups, 2, MI.getLoc()));
    return;
  case RISCC::CALL16:
  case RISCC::JMP16:
    Word = rr(Opcode == RISCC::CALL16 ? 7 : 0, 0, 0x1f, 5);
    emit16(Code, Word);
    emit16(Code,
           codeImmediate(MI.getOperand(0), Fixups, 2, MI.getLoc()));
    return;
  case RISCC::CALL:
    Word = rr(7, reg(MI.getOperand(0)), 0x1f, 1);
    break;
  case RISCC::RETS:
    Word = rr(0, 7, 0x1f, 0);
    break;
  case RISCC::MOV:
    Word = rr(reg(MI.getOperand(0)), reg(MI.getOperand(1)), 5,
              reg(MI.getOperand(1)));
    break;
  case RISCC::NOP:
    Word = rr(0, 0, 5, 0);
    break;
  case RISCC::HALT:
    Word = ri(4, 7, 0xff);
    break;
  case RISCC::LI: {
    const unsigned Destination = reg(MI.getOperand(0));
    const MCOperand &Imm = MI.getOperand(1);
    if (Imm.isImm()) {
      const unsigned Value = Imm.getImm();
      emit16(Code, ri(Destination, 1, Value >> 8));
      emit16(Code, ri(Destination, 5, Value));
    } else {
      const MCExpr *Expr = Imm.getExpr();
      RISCCMCExpr::VariantKind Variant = RISCCMCExpr::VK_None;
      if (const auto *RE = dyn_cast<RISCCMCExpr>(Expr)) {
        Variant = RE->getKind();
        if (Variant != RISCCMCExpr::VK_CODE &&
            Variant != RISCCMCExpr::VK_TPOFF)
          Ctx.reportError(
              MI.getLoc(),
              "LI accepts only an unmodified, code(), or tpoff() expression");
        Expr = RE->getSubExpr();
      }
      RISCC::Fixups HiFixup = RISCC::fixup_hi8;
      RISCC::Fixups LoFixup = RISCC::fixup_lo8;
      if (Variant == RISCCMCExpr::VK_CODE) {
        HiFixup = RISCC::fixup_code_hi8;
        LoFixup = RISCC::fixup_code_lo8;
      } else if (Variant == RISCCMCExpr::VK_TPOFF) {
        HiFixup = RISCC::fixup_tpoff_hi8;
        LoFixup = RISCC::fixup_tpoff_lo8;
      }
      Fixups.push_back(MCFixup::create(0, Expr, HiFixup));
      Fixups.push_back(MCFixup::create(2, Expr, LoFixup));
      emit16(Code, ri(Destination, 1, 0));
      emit16(Code, ri(Destination, 5, 0));
    }
    return;
  }
  default:
    Ctx.reportError(MI.getLoc(), "unsupported RISC-C instruction encoding");
    return;
  }
  emit16(Code, Word);
}

MCCodeEmitter *llvm::createRISCCMCCodeEmitter(const MCInstrInfo &,
                                               MCContext &Ctx) {
  return new RISCCMCCodeEmitter(Ctx);
}
