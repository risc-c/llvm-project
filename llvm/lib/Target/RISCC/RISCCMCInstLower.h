//===-- RISCCMCInstLower.h - Lower MachineInstr to MCInst -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCMCINSTLOWER_H
#define LLVM_LIB_TARGET_RISCC_RISCCMCINSTLOWER_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class AsmPrinter;
class MCContext;
class MCInst;
class MCOperand;
class MCSymbol;
class MachineInstr;
class MachineOperand;

class LLVM_LIBRARY_VISIBILITY RISCCMCInstLower {
  MCContext &Ctx;
  AsmPrinter &Printer;

public:
  RISCCMCInstLower(MCContext &Ctx, AsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}
  void lower(const MachineInstr *, MCInst &) const;
  MCOperand lowerSymbolOperand(const MachineOperand &, MCSymbol *) const;
};
} // namespace llvm

#endif
