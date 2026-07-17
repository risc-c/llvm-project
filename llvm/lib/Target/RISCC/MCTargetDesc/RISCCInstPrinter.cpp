//===-- RISCCInstPrinter.cpp - RISCC MCInst Printer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCInstPrinter.h"
#include "RISCCMCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

#include "RISCCGenAsmWriter.inc"

void RISCCInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  OS << getRegisterName(Reg);
}

void RISCCInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                    const MCSubtargetInfo &, raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg())
    printRegName(OS, Op.getReg());
  else if (Op.isImm())
    OS << Op.getImm();
  else
    MAI.printExpr(OS, *Op.getExpr());
}

void RISCCInstPrinter::printBranchOperand(const MCInst *MI, unsigned OpNo,
                                          const MCSubtargetInfo &,
                                          raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm()) {
    int64_t Delta = Op.getImm() * 2 + 2;
    OS << '.';
    if (Delta)
      OS << (Delta > 0 ? "+" : "") << Delta;
  } else
    MAI.printExpr(OS, *Op.getExpr());
}

void RISCCInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                 StringRef Annot,
                                 const MCSubtargetInfo &STI,
                                 raw_ostream &OS) {
  printInstruction(MI, Address, STI, OS);
  printAnnotation(OS, Annot);
}
