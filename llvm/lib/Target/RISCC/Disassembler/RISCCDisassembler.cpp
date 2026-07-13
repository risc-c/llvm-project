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

static MCRegister gpr(unsigned Number) {
  static const MCRegister R[] = {RISCC::R0, RISCC::R1, RISCC::R2, RISCC::R3,
                                 RISCC::R4, RISCC::R5, RISCC::R6, RISCC::R7};
  return R[Number & 7];
}
static MCRegister sreg(unsigned Number) {
  static const MCRegister R[] = {RISCC::S0, RISCC::S1, RISCC::S2, RISCC::S3,
                                 RISCC::S4, RISCC::S5, RISCC::S6, RISCC::S7};
  return R[Number & 7];
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
  if (Bytes.size() < 2)
    return Fail;
  const uint16_t Word = support::endian::read16le(Bytes.data());
  const unsigned Major = Word >> 14;
  const unsigned Destination = (Word >> 11) & 7;
  const unsigned Source = (Word >> 8) & 7;
  const unsigned Immediate = Word & 0xff;
  const unsigned Function = (Word >> 3) & 0x1f;
  const unsigned Operand = Word & 7;
  Size = 2;

  if (Major < 2) {
    MI.setOpcode(Major ? RISCC::STW : RISCC::LDW);
    addReg(MI, gpr(Destination));
    addReg(MI, gpr(Source));
    addImm(MI, static_cast<int8_t>(Immediate));
    return Success;
  }
  if (Major == 2) {
    if (Source == 7) {
      static const unsigned Br[] = {RISCC::BEQZ, RISCC::BNEZ, RISCC::BLTZ,
                                    RISCC::BGEZ, RISCC::JMP8};
      if (Destination >= std::size(Br))
        return Fail;
      MI.setOpcode(Br[Destination]);
      addImm(MI, static_cast<int8_t>(Immediate));
      return Success;
    }
    static const unsigned ImmediateOpcodes[] = {
        RISCC::LDI,  RISCC::LUI, RISCC::ADDI, RISCC::CMPI,
        RISCC::ANDI, RISCC::ORI, RISCC::XORI,
    };
    if (Source >= std::size(ImmediateOpcodes))
      return Fail;
    MI.setOpcode(ImmediateOpcodes[Source]);
    addReg(MI, gpr(Destination));
    if (Source == 2 || Source == 4 || Source == 5 || Source == 6)
      addReg(MI, gpr(Destination)); // Tied source operand.
    if (Source == 2 || Source == 3)
      addImm(MI, static_cast<int8_t>(Immediate));
    else
      addImm(MI, Immediate);
    return Success;
  }

  if (Function == 0x1f) {
    switch (Operand) {
    case 0:
      if (Destination == 0)
        MI.setOpcode(RISCC::RET);
      else if (Destination == 7 && STI.hasFeature(RISCC::FeatureSys))
        MI.setOpcode(RISCC::RETI);
      else
        return Fail;
      addReg(MI, sreg(Source));
      return Success;
    case 1:
      MI.setOpcode(RISCC::JAL);
      addReg(MI, sreg(Destination));
      addReg(MI, gpr(Source));
      return Success;
    case 2:
      MI.setOpcode(RISCC::MFS);
      addReg(MI, gpr(Destination));
      addReg(MI, sreg(Source));
      return Success;
    case 3:
      MI.setOpcode(RISCC::MTS);
      addReg(MI, sreg(Destination));
      addReg(MI, gpr(Source));
      return Success;
    case 5:
      if (!STI.hasFeature(RISCC::FeatureSys) || Source || Bytes.size() < 4)
        return Fail;
      if (support::endian::read16le(Bytes.data() + 2) & 0x8000)
        return Fail;
      MI.setOpcode(RISCC::JAL16);
      addReg(MI, sreg(Destination));
      addImm(MI, uint64_t(support::endian::read16le(Bytes.data() + 2)) * 2);
      Size = 4;
      return Success;
    case 6:
      if (!STI.hasFeature(RISCC::FeatureSys) || Source ||
          (Destination != 0 && Destination != 7))
        return Fail;
      MI.setOpcode(Destination == 0 ? RISCC::CLI : RISCC::STI);
      return Success;
    }
  }

  static const unsigned RROps[16] = {
      RISCC::ADD,  RISCC::SUB,  RISCC::SLT,  RISCC::SLTU,
      RISCC::AND,  RISCC::OR,   RISCC::XOR,  RISCC::MUL,
      RISCC::LDWX, 0,           RISCC::LDB,  RISCC::STB,
      RISCC::SHRI, RISCC::SARI, RISCC::LDBS, RISCC::SHLI,
  };
  if (Function >= std::size(RROps) || !RROps[Function])
    return Fail;
  if (Function == 7 && !STI.hasFeature(RISCC::FeatureMul))
    return Fail;
  if (Function == 0x0f && !STI.hasFeature(RISCC::FeatureWideShift))
    return Fail;
  if ((Function == 0x0c || Function == 0x0d) && Operand != 0 &&
      !STI.hasFeature(RISCC::FeatureWideShift))
    return Fail;
  MI.setOpcode(RROps[Function]);
  addReg(MI, gpr(Destination));
  addReg(MI, gpr(Source));
  if (Function == 0x0b) {
    if (Operand)
      return Fail;
  } else if (Function == 0x0c || Function == 0x0d || Function == 0x0f) {
    addImm(MI, Operand + 1);
  } else {
    addReg(MI, gpr(Operand));
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
