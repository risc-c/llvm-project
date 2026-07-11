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
}

#endif
