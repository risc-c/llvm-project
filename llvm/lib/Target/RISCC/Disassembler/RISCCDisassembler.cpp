//===-- RISCCDisassembler.cpp - RISCC Disassembler ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "TargetInfo/RISCCTargetInfo.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::MCD;

#define DEBUG_TYPE "riscc-disassembler"

using DecodeStatus = MCDisassembler::DecodeStatus;

namespace {
class RISCCDisassembler final : public MCDisassembler {
public:
  RISCCDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &, uint64_t &, ArrayRef<uint8_t>, uint64_t,
                              raw_ostream &) const override;
};
} // namespace

static constexpr MCRegister GPRDecoderTable[] = {
    RISCC::R0, RISCC::R1, RISCC::R2, RISCC::R3,
    RISCC::R4, RISCC::R5, RISCC::R6, RISCC::R7,
};

static constexpr MCRegister SREGDecoderTable[] = {
    RISCC::S0, RISCC::S1, RISCC::S2, RISCC::S3,
    RISCC::S4, RISCC::S5, RISCC::S6, RISCC::S7,
};

static DecodeStatus DecodeGPRRegisterClass(MCInst &MI, uint64_t RegNo,
                                           uint64_t,
                                           const MCDisassembler *) {
  if (RegNo >= std::size(GPRDecoderTable))
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createReg(GPRDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPR32RegisterClass(MCInst &MI, uint64_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  return DecodeGPRRegisterClass(MI, RegNo, Address, Decoder);
}

static DecodeStatus DecodeSREGRegisterClass(MCInst &MI, uint64_t RegNo,
                                            uint64_t,
                                            const MCDisassembler *) {
  if (RegNo >= std::size(SREGDecoderTable))
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createReg(SREGDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus decodeSigned8(MCInst &MI, uint64_t Value, uint64_t,
                                  const MCDisassembler *) {
  MI.addOperand(MCOperand::createImm(static_cast<int8_t>(Value)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeBranch8(MCInst &MI, uint64_t Value, uint64_t,
                                  const MCDisassembler *) {
  uint8_t Encoded = static_cast<uint8_t>(Value);
  uint8_t Rel = static_cast<uint8_t>((Encoded >> 1) | (Encoded << 7));
  MI.addOperand(MCOperand::createImm(static_cast<int8_t>(Rel)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeRC32WordDisp(MCInst &MI, uint64_t Value, uint64_t,
                                       const MCDisassembler *) {
  uint8_t Encoded = static_cast<uint8_t>(Value);
  uint8_t Words = static_cast<uint8_t>(((Encoded >> 2) & 0x3f) |
                                       ((Encoded & 0x02) << 5));
  MI.addOperand(MCOperand::createImm(SignExtend32<7>(Words) * 4));
  return MCDisassembler::Success;
}

static DecodeStatus decodeShiftAmount(MCInst &MI, uint64_t Value, uint64_t,
                                      const MCDisassembler *Decoder) {
  bool HasWideShift =
      Decoder->getSubtargetInfo().hasFeature(RISCC::FeatureWideShift);
  if (!HasWideShift &&
      (MI.getOpcode() == RISCC::SLLI || MI.getOpcode() == RISCC::SLLI32 ||
       Value != 0))
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createImm(Value + 1));
  return MCDisassembler::Success;
}

static DecodeStatus decodeCodeTarget(MCInst &MI, uint64_t Value, uint64_t,
                                     const MCDisassembler *Decoder) {
  if (!Decoder->getSubtargetInfo().hasFeature(RISCC::FeatureRC32) &&
      Value > 0xffff)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createImm(Value));
  return MCDisassembler::Success;
}

#include "RISCCGenDisassemblerTables.inc"

DecodeStatus RISCCDisassembler::getInstruction(
    MCInst &MI, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t Address,
    raw_ostream &) const {
  if (Bytes.size() < 2)
    return Fail;

  uint16_t Instruction = support::endian::read16le(Bytes.data());
  Size = 2;
  bool IsJALL = (Instruction & 0xc03f) == 0x0034;
  DecodeStatus Result = Fail;
  if (STI.hasFeature(RISCC::FeatureNano))
    Result = decodeInstruction(DecoderTableNano16, MI, Instruction, Address,
                               this, STI);
  else if (STI.hasFeature(RISCC::FeatureRC32))
    Result = decodeInstruction(DecoderTableRC3216, MI, Instruction, Address,
                               this, STI);
  else
    Result = decodeInstruction(DecoderTableRC1616, MI, Instruction, Address,
                               this, STI);
  if (Result == Fail) {
    MI.clear();
    Result =
        decodeInstruction(DecoderTable16, MI, Instruction, Address, this, STI);
  }
  if (Result != Fail && !IsJALL)
    return Result;

  // Only the defined JALL head can begin a 32-bit instruction. Avoid
  // consuming the following instruction after a reserved 00 encoding.
  if (!IsJALL || !STI.hasFeature(RISCC::FeatureLongJall) || Bytes.size() < 4)
    return Fail;
  MI.clear();
  uint32_t LongInstruction = support::endian::read32le(Bytes.data());
  Size = 4;
  Result = decodeInstruction(DecoderTable32, MI, LongInstruction, Address, this,
                             STI);
  return Result;
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
