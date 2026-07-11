#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "TargetInfo/RISCCTargetInfo.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;

namespace {
class RISCCDisassembler final : public MCDisassembler {
  const MCSubtargetInfo &STI;
public:
  RISCCDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx), STI(STI) {}
  DecodeStatus getInstruction(MCInst &, uint64_t &, ArrayRef<uint8_t>,
                              uint64_t, raw_ostream &) const override;
};

static MCRegister gpr(unsigned N) {
  static const MCRegister R[] = {RISCC::R0, RISCC::R1, RISCC::R2, RISCC::R3,
                                 RISCC::R4, RISCC::R5, RISCC::R6, RISCC::R7};
  return R[N & 7];
}
static MCRegister sreg(unsigned N) {
  static const MCRegister R[] = {RISCC::S0, RISCC::S1, RISCC::S2, RISCC::S3,
                                 RISCC::S4, RISCC::S5, RISCC::S6, RISCC::S7};
  return R[N & 7];
}
static void addReg(MCInst &MI, MCRegister R) {
  MI.addOperand(MCOperand::createReg(R));
}
static void addImm(MCInst &MI, int64_t V) {
  MI.addOperand(MCOperand::createImm(V));
}
}

MCDisassembler::DecodeStatus RISCCDisassembler::getInstruction(
    MCInst &MI, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t,
    raw_ostream &) const {
  if (Bytes.size() < 2) return Fail;
  uint16_t W = support::endian::read16le(Bytes.data());
  unsigned Major = W >> 14;
  unsigned D = (W >> 11) & 7, A = (W >> 8) & 7;
  unsigned Imm = W & 0xff, F = (W >> 3) & 0x1f, B = W & 7;
  Size = 2;

  if (Major < 2) {
    MI.setOpcode(Major ? RISCC::STW : RISCC::LDW);
    addReg(MI, gpr(D)); addReg(MI, gpr(A)); addImm(MI, int8_t(Imm));
    return Success;
  }
  if (Major == 2) {
    if (A == 7) {
      static const unsigned Br[] = {RISCC::BEQZ, RISCC::BNEZ, RISCC::BLTZ,
                                    RISCC::BGEZ, RISCC::JMP8};
      if (D >= std::size(Br)) return Fail;
      MI.setOpcode(Br[D]); addImm(MI, int8_t(Imm)); return Success;
    }
    static const unsigned IOps[] = {RISCC::LDI, RISCC::LUI, RISCC::ADDI,
      RISCC::CMPI, RISCC::ANDI, RISCC::ORI, RISCC::XORI};
    if (A >= std::size(IOps)) return Fail;
    MI.setOpcode(IOps[A]); addReg(MI, gpr(D));
    if (A == 2 || A == 4 || A == 5 || A == 6)
      addReg(MI, gpr(D)); // tied source operand
    if (A == 2 || A == 3) addImm(MI, int8_t(Imm)); else addImm(MI, Imm);
    return Success;
  }

  if (F == 0x1f) {
    switch (B) {
    case 0:
      if (D) return Fail;
      MI.setOpcode(RISCC::RET); addReg(MI, sreg(A)); return Success;
    case 1:
      MI.setOpcode(RISCC::JAL); addReg(MI, sreg(D)); addReg(MI, gpr(A));
      return Success;
    case 2:
      MI.setOpcode(RISCC::MFS); addReg(MI, gpr(D)); addReg(MI, sreg(A));
      return Success;
    case 3:
      MI.setOpcode(RISCC::MTS); addReg(MI, sreg(D)); addReg(MI, gpr(A));
      return Success;
    case 4:
      if (!STI.hasFeature(RISCC::FeatureSys)) return Fail;
      if (D) return Fail;
      MI.setOpcode(RISCC::RETI); addReg(MI, sreg(A)); return Success;
    case 5:
      if (!STI.hasFeature(RISCC::FeatureSys) || A || Bytes.size() < 4)
        return Fail;
      if (support::endian::read16le(Bytes.data() + 2) & 0x8000) return Fail;
      MI.setOpcode(RISCC::JAL16); addReg(MI, sreg(D));
      addImm(MI, uint64_t(support::endian::read16le(Bytes.data() + 2)) * 2);
      Size = 4; return Success;
    case 6: case 7:
      if (!STI.hasFeature(RISCC::FeatureSys) || D || A) return Fail;
      MI.setOpcode(B == 6 ? RISCC::CLI : RISCC::STI); return Success;
    }
  }

  static const unsigned RROps[16] = {
    RISCC::ADD, RISCC::SUB, RISCC::SLT, RISCC::SLTU,
    RISCC::AND, RISCC::OR, RISCC::XOR, RISCC::MUL,
    RISCC::LDWX, 0, RISCC::LDB, RISCC::STB,
    RISCC::SHRI, RISCC::SARI, RISCC::LDBS, RISCC::SHLI
  };
  if (F >= std::size(RROps) || !RROps[F]) return Fail;
  if (F == 7 && !STI.hasFeature(RISCC::FeatureMul)) return Fail;
  if (F == 0x0f && !STI.hasFeature(RISCC::FeatureWideShift)) return Fail;
  if ((F == 0x0c || F == 0x0d) && B != 0 &&
      !STI.hasFeature(RISCC::FeatureWideShift)) return Fail;
  MI.setOpcode(RROps[F]);
  addReg(MI, gpr(D)); addReg(MI, gpr(A));
  if (F == 0x0b) {
    if (B) return Fail;
  } else if (F == 0x0c || F == 0x0d || F == 0x0f) {
    addImm(MI, B + 1);
  } else {
    addReg(MI, gpr(B));
  }
  return Success;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeRISCCDisassembler() {
  TargetRegistry::RegisterMCDisassembler(
      getTheRISCCTarget(),
      [](const Target &, const MCSubtargetInfo &STI,
         MCContext &Ctx) -> MCDisassembler * {
        return new RISCCDisassembler(STI, Ctx);
      });
}
