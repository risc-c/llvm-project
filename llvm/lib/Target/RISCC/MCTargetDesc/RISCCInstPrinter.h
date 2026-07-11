#ifndef LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCINSTPRINTER_H
#define LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCINSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {
class RISCCInstPrinter : public MCInstPrinter {
public:
  RISCCInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                   const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  void printInst(const MCInst *, uint64_t, StringRef,
                 const MCSubtargetInfo &, raw_ostream &) override;
  void printRegName(raw_ostream &, MCRegister) override;
  void printOperand(const MCInst *, unsigned, const MCSubtargetInfo &,
                    raw_ostream &);
  void printBranchOperand(const MCInst *, unsigned, const MCSubtargetInfo &,
                          raw_ostream &);

  std::pair<const char *, uint64_t>
  getMnemonic(const MCInst &MI) const override;
  void printInstruction(const MCInst *, uint64_t, const MCSubtargetInfo &,
                        raw_ostream &);
  static const char *getRegisterName(MCRegister Reg);
};
}

#endif
