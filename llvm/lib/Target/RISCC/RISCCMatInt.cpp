//===-- RISCCMatInt.cpp - Bounded integer materialization -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCMatInt.h"
#include "RISCC.h"
#include "RISCCSubtarget.h"
#include "llvm/Support/ErrorHandling.h"

#include <cassert>

using namespace llvm;

namespace {

struct Seed {
  RISCCMatInt::Inst Instruction;
  APInt Value;
};

enum class OperationKind : unsigned { Add, Or, Xor, And, Shift };

struct Candidate {
  RISCCMatInt::Inst Instruction;
  OperationKind Kind;
  unsigned ShiftAmount = 0;
  unsigned SeedOrder = 0;
  unsigned AddMagnitude = 0;
};

struct BitwiseOp {
  unsigned Opcode;
  OperationKind Kind;
  int64_t Immediate;
};

bool isBetter(const Candidate &A, const Candidate &B) {
  if (A.Kind != B.Kind)
    return static_cast<unsigned>(A.Kind) < static_cast<unsigned>(B.Kind);
  if (A.Kind == OperationKind::Add && A.AddMagnitude != B.AddMagnitude)
    return A.AddMagnitude < B.AddMagnitude;
  if (A.Kind == OperationKind::Shift && A.ShiftAmount != B.ShiftAmount)
    return A.ShiftAmount < B.ShiftAmount;
  return A.SeedOrder < B.SeedOrder;
}

APInt apply(unsigned Opcode, const APInt &Value, int64_t Immediate) {
  unsigned Width = Value.getBitWidth();
  switch (Opcode) {
  case RISCC::ADDI:
    return Value + APInt(Width, static_cast<uint64_t>(Immediate), true);
  case RISCC::ANDI:
    return Value & APInt(Width, static_cast<uint64_t>(Immediate));
  case RISCC::ORI:
    return Value | APInt(Width, static_cast<uint64_t>(Immediate));
  case RISCC::XORI:
    return Value ^ APInt(Width, static_cast<uint64_t>(Immediate));
  case RISCC::SLLI:
    return Value.shl(static_cast<unsigned>(Immediate));
  case RISCC::SRLI:
    return Value.lshr(static_cast<unsigned>(Immediate));
  case RISCC::SRAI:
    return Value.ashr(static_cast<unsigned>(Immediate));
  default:
    llvm_unreachable("unexpected RISCC materialization opcode");
  }
}

} // namespace

namespace llvm::RISCCMatInt {

SmallVector<Inst, 2> generate(const APInt &Value, const RISCCSubtarget &STI,
                              unsigned MaxInstructions) {
  assert((Value.getBitWidth() == 16 || Value.getBitWidth() == 32) &&
         "RISCC materialization requires a native-width value");
  const unsigned NativeBits = STI.isRC32() ? 32 : 16;
  assert(Value.getBitWidth() == NativeBits &&
         "RISCC materialization value must match the subtarget width");
  assert(MaxInstructions >= 1 && MaxInstructions <= 2 &&
         "RISCC materialization supports one or two instructions");

  // Selection asks this for every constant. Avoid searching when a single
  // immediate instruction suffices, or when that is the caller's whole budget.
  if (Value.isIntN(8))
    return {{RISCC::LDI, static_cast<int64_t>(Value.getZExtValue())}};
  if (!STI.isRC32() && Value.trunc(8).isZero())
    return {{RISCC::LUI, static_cast<int64_t>(Value.lshr(8).getZExtValue())}};
  if (MaxInstructions == 1)
    return {};

  SmallVector<Seed, 512> Seeds;
  for (unsigned Immediate = 0; Immediate != 256; ++Immediate)
    Seeds.push_back({{RISCC::LDI, static_cast<int64_t>(Immediate)},
                     APInt(NativeBits, Immediate)});
  if (!STI.isRC32())
    for (unsigned Immediate = 0; Immediate != 256; ++Immediate)
      Seeds.push_back({{RISCC::LUI, static_cast<int64_t>(Immediate)},
                       APInt(NativeBits, Immediate).shl(8)});

  Candidate Best{{}, OperationKind::Shift};
  bool HaveBest = false;
  const APInt LowBits = Value.trunc(8);
  const unsigned MaxShift = STI.hasWideShift() ? 8 : 1;

  for (unsigned SeedOrder = 0; SeedOrder != Seeds.size(); ++SeedOrder) {
    const Seed &Seed = Seeds[SeedOrder];

    APInt Difference = Value - Seed.Value;
    if (Difference.isSignedIntN(8)) {
      int64_t Immediate = Difference.getSExtValue();
      APInt Result = apply(RISCC::ADDI, Seed.Value, Immediate);
      if (Result == Value) {
        Candidate Current{
            {RISCC::ADDI, Immediate},
            OperationKind::Add,
            0,
            SeedOrder,
            static_cast<unsigned>(Immediate < 0 ? -Immediate : Immediate)};
        if (!HaveBest || isBetter(Current, Best)) {
          Best = Current;
          HaveBest = true;
        }
      }
    }

    const APInt SeedLowBits = Seed.Value.trunc(8);
    const BitwiseOp BitwiseOps[] = {
        {RISCC::ORI, OperationKind::Or,
         static_cast<int64_t>(LowBits.getZExtValue())},
        {RISCC::XORI, OperationKind::Xor,
         static_cast<int64_t>((SeedLowBits ^ LowBits).getZExtValue())},
        {RISCC::ANDI, OperationKind::And,
         static_cast<int64_t>(LowBits.getZExtValue())}};
    for (const BitwiseOp &Op : BitwiseOps) {
      APInt Result = apply(Op.Opcode, Seed.Value, Op.Immediate);
      if (Result != Value)
        continue;
      Candidate Current{{Op.Opcode, Op.Immediate}, Op.Kind, 0, SeedOrder};
      if (!HaveBest || isBetter(Current, Best)) {
        Best = Current;
        HaveBest = true;
      }
    }

    for (unsigned Amount = 1; Amount <= MaxShift; ++Amount) {
      for (unsigned Opcode : {RISCC::SLLI, RISCC::SRLI, RISCC::SRAI}) {
        if (Opcode == RISCC::SLLI && STI.isNano())
          continue;
        APInt Result = apply(Opcode, Seed.Value, Amount);
        if (Result != Value)
          continue;
        Candidate Current{{Opcode, static_cast<int64_t>(Amount)},
                          OperationKind::Shift,
                          Amount,
                          SeedOrder};
        if (!HaveBest || isBetter(Current, Best)) {
          Best = Current;
          HaveBest = true;
        }
      }
    }
  }

  if (!HaveBest)
    return {};
  return {Seeds[Best.SeedOrder].Instruction, Best.Instruction};
}

} // namespace llvm::RISCCMatInt
