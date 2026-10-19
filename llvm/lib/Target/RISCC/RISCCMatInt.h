//===-- RISCCMatInt.h - Bounded integer materialization --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCMATINT_H
#define LLVM_LIB_TARGET_RISCC_RISCCMATINT_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
class RISCCSubtarget;

namespace RISCCMatInt {

struct Inst {
  unsigned Opcode;
  int64_t Immediate;
};

// Search the bounded, policy-neutral set of native-width immediate sequences.
// The result uses RC16/base opcodes; callers select the native opcode before
// emitting each instruction.
SmallVector<Inst, 2> generate(const APInt &Value, const RISCCSubtarget &STI,
                              unsigned MaxInstructions = 2);

} // namespace RISCCMatInt
} // namespace llvm

#endif
