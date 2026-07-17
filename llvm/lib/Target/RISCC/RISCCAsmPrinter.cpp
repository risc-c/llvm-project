//===-- RISCCAsmPrinter.cpp - RISCC Assembly Printer ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCAsmPrinter.h"
#include "RISCC.h"
#include "RISCCMCInstLower.h"
#include "TargetInfo/RISCCTargetInfo.h"
#include "MCTargetDesc/RISCCInstPrinter.h"
#include "MCTargetDesc/RISCCMCExpr.h"
#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/AsmPrinterAnalysis.h"
#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/PassManager.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "riscc-asm-printer"

namespace {
class RISCCAsmPrinter final : public AsmPrinter {
public:
  static char ID;
  RISCCAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> S)
      : AsmPrinter(TM, std::move(S), ID) {}
  StringRef getPassName() const override { return "RISC-C Assembly Printer"; }
  bool runOnMachineFunction(MachineFunction &MF) override {
    SetupMachineFunction(MF);
    emitFunctionBody();
    return false;
  }
  void emitInstruction(const MachineInstr *MI) override {
    RISCC_MC::verifyInstructionPredicates(MI->getOpcode(),
                                           getSubtargetInfo().getFeatureBits());
    MCInst Out;
    RISCCMCInstLower(OutContext, *this).lower(MI, Out);
    unsigned Opcode = MI->getOpcode();
    if (Opcode == RISCC::CALL_MIN || Opcode == RISCC::CALL_NANO ||
        Opcode == RISCC::TAIL_MIN || Opcode == RISCC::TAIL_NANO) {
      bool IsNano = Opcode == RISCC::CALL_NANO || Opcode == RISCC::TAIL_NANO;
      bool IsTail = Opcode == RISCC::TAIL_MIN || Opcode == RISCC::TAIL_NANO;
      MCInst Address = MCInstBuilder(RISCC::LI)
                           .addReg(RISCC::R0)
                           .addOperand(Out.getOperand(0));
      EmitToStreamer(*OutStreamer, Address);

      MCRegister Link = IsTail ? (IsNano ? RISCC::R0 : RISCC::S0)
                               : (IsNano ? RISCC::R6 : RISCC::S7);
      MCInst Transfer = MCInstBuilder(IsNano ? RISCC::JAL_NANO : RISCC::JAL)
                            .addReg(Link)
                            .addReg(RISCC::R0);
      EmitToStreamer(*OutStreamer, Transfer);
      return;
    }
    EmitToStreamer(*OutStreamer, Out);
  }
  const MCExpr *lowerConstant(const Constant *CV, const Constant *BaseCV,
                              uint64_t Offset) override {
    const MCExpr *Expr = nullptr;
    // Inspect the value being emitted, not BaseCV.  For a one-use function
    // constant, generic aggregate emission sets BaseCV to the containing AS0
    // GlobalVariable; using it would silently turn a function pointer into an
    // ABS16 data relocation.  Offset describes the initializer's storage
    // position and must not be added to the pointed-to function address.
    if (const auto *GV = dyn_cast<GlobalValue>(CV)) {
      if (isa<Function>(GV) || GV->getAddressSpace() == 1)
        Expr = MCSymbolRefExpr::create(getSymbol(GV), OutContext);
    } else if (const auto *BA = dyn_cast<BlockAddress>(CV)) {
      Expr = MCSymbolRefExpr::create(GetBlockAddressSymbol(BA), OutContext);
    }
    if (!Expr)
      return AsmPrinter::lowerConstant(CV, BaseCV, Offset);
    return RISCCMCExpr::create(RISCCMCExpr::VK_CODE, Expr, OutContext);
  }
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override {
    if (ExtraCode && ExtraCode[0])
      return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS);
    const MachineOperand &MO = MI->getOperand(OpNo);
    if (MO.isReg())
      OS << RISCCInstPrinter::getRegisterName(MO.getReg());
    else if (MO.isImm())
      OS << MO.getImm();
    else
      return true;
    return false;
  }
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override {
    if (ExtraCode && ExtraCode[0])
      return true;
    OS << '[' << RISCCInstPrinter::getRegisterName(MI->getOperand(OpNo).getReg())
       << ']';
    return false;
  }
};
}

char RISCCAsmPrinter::ID = 0;
INITIALIZE_PASS(RISCCAsmPrinter, DEBUG_TYPE, "RISC-C Assembly Printer", false,
                false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeRISCCAsmPrinter() {
  RegisterAsmPrinter<RISCCAsmPrinter> X(getTheRISCCTarget());
}

PreservedAnalyses RISCCAsmPrinterBeginPass::run(Module &M,
                                                ModuleAnalysisManager &MAM) {
  auto &AP = static_cast<RISCCAsmPrinter &>(
      MAM.getResult<AsmPrinterAnalysis>(M).getPrinter());
  setupModuleAsmPrinter(M, MAM, AP);
  AP.doInitialization(M);
  return PreservedAnalyses::all();
}
PreservedAnalyses RISCCAsmPrinterPass::run(
    MachineFunction &MF, MachineFunctionAnalysisManager &MFAM) {
  auto &AP = static_cast<RISCCAsmPrinter &>(
      MFAM.getResult<ModuleAnalysisManagerMachineFunctionProxy>(MF)
          .getCachedResult<AsmPrinterAnalysis>(*MF.getFunction().getParent())
          ->getPrinter());
  setupMachineFunctionAsmPrinter(MFAM, MF, AP);
  AP.runOnMachineFunction(MF);
  return PreservedAnalyses::all();
}
PreservedAnalyses RISCCAsmPrinterEndPass::run(Module &M,
                                              ModuleAnalysisManager &MAM) {
  auto &AP = static_cast<RISCCAsmPrinter &>(
      MAM.getResult<AsmPrinterAnalysis>(M).getPrinter());
  setupModuleAsmPrinter(M, MAM, AP);
  AP.doFinalization(M);
  return PreservedAnalyses::all();
}
