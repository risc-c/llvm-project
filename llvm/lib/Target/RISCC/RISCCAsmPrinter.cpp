//===-- RISCCAsmPrinter.cpp - RISCC Assembly Printer ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCAsmPrinter.h"
#include "MCTargetDesc/RISCCInstPrinter.h"
#include "MCTargetDesc/RISCCMCExpr.h"
#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "RISCC.h"
#include "RISCCConstantPoolValue.h"
#include "RISCCInstrInfo.h"
#include "RISCCMCInstLower.h"
#include "RISCCSubtarget.h"
#include "TargetInfo/RISCCTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/AsmPrinterAnalysis.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/Analysis.h"
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
  void emitConstantPool() override {}
  void emitMachineConstantPoolValue(MachineConstantPoolValue *Value) override {
    auto *Symbol = static_cast<RISCCConstantPoolSymbol *>(Value);
    const MCExpr *Expr = MCSymbolRefExpr::create(
        Symbol->getGlobal() ? getSymbol(Symbol->getGlobal())
                            : GetExternalSymbolSymbol(Symbol->getSymbol()),
        OutContext);
    if (Symbol->isTPOFF())
      Expr = RISCCMCExpr::create(RISCCMCExpr::VK_TPOFF, Expr, OutContext);
    else if (Symbol->isCallTarget())
      Expr = RISCCMCExpr::create(RISCCMCExpr::VK_CALL_TARGET, Expr, OutContext);
    OutStreamer->emitValue(Expr, 4);
  }
  void emitLiteralCall(const MachineInstr *MI, MCRegister Link) {
    MCSymbol *Literal = MI->getOperand(0).getMCSymbol();
    assert(Literal && "direct call was not assigned a literal pool");
    if (MF->getSubtarget<RISCCSubtarget>().hasLongJall()) {
      MCSymbol *Site = OutContext.createTempSymbol();
      const MCExpr *SiteExpr = MCSymbolRefExpr::create(Site, OutContext);
      const MCExpr *LiteralExpr = MCSymbolRefExpr::create(Literal, OutContext);
      OutStreamer->emitRelocDirective(*SiteExpr,
                                      Link == RISCC::S7 ? "R_RISCC_RELAX_CALL"
                                                        : "R_RISCC_RELAX_TAIL",
                                      LiteralExpr);
      OutStreamer->emitLabel(Site);
    }
    EmitToStreamer(*OutStreamer,
                   MCInstBuilder(RISCC::LDPC)
                       .addReg(RISCC::R0)
                       .addExpr(MCSymbolRefExpr::create(Literal, OutContext)));
    EmitToStreamer(*OutStreamer,
                   MCInstBuilder(RISCC::JALR).addReg(Link).addReg(RISCC::R0));
  }
  void emitInstruction(const MachineInstr *MI) override {
    if (MI->getOpcode() == RISCC::CONSTPOOL_ENTRY) {
      OutStreamer->emitLabel(MI->getOperand(0).getMCSymbol());
      const MachineOperand &Value = MI->getOperand(1);
      if (Value.isCPI()) {
        const MachineConstantPoolEntry &Constant =
            MF->getConstantPool()->getConstants()[Value.getIndex()];
        if (Constant.isMachineConstantPoolEntry())
          emitMachineConstantPoolValue(Constant.Val.MachineCPVal);
        else
          emitGlobalConstant(getDataLayout(), Constant.Val.ConstVal);
      } else {
        OutStreamer->emitValue(
            MCSymbolRefExpr::create(Value.getMBB()->getSymbol(), OutContext),
            4);
      }
      return;
    }
    if (MI->getOpcode() == RISCC::CALL32_LITERAL ||
        MI->getOpcode() == RISCC::TAIL32_LITERAL) {
      RISCC_MC::verifyInstructionPredicates(
          MI->getOpcode(), getSubtargetInfo().getFeatureBits());
      emitLiteralCall(MI, MI->getOpcode() == RISCC::CALL32_LITERAL ? RISCC::S7
                                                                   : RISCC::S0);
      return;
    }
    MCInst Out;
    RISCCMCInstLower(OutContext, *this).lower(MI, Out);
    unsigned Opcode = MI->getOpcode();

    RISCC_MC::verifyInstructionPredicates(MI->getOpcode(),
                                          getSubtargetInfo().getFeatureBits());

    MCRegister Link;
    unsigned TransferOpcode = RISCC::JALR;
    switch (Opcode) {
    default:
      break;
    case RISCC::RETS:
      EmitToStreamer(*OutStreamer, MCInstBuilder(RISCC::RET).addReg(RISCC::S7));
      return;
    case RISCC::RET_NANO:
      EmitToStreamer(*OutStreamer, MCInstBuilder(RISCC::JALR_NANO)
                                       .addReg(RISCC::R0)
                                       .addOperand(Out.getOperand(0)));
      return;
    case RISCC::LINK_S3_RET:
      EmitToStreamer(*OutStreamer, MCInstBuilder(RISCC::RET).addReg(RISCC::S3));
      return;
    case RISCC::CALL:
    case RISCC::CALL32:
    case RISCC::CALL_MIN:
      Link = RISCC::S7;
      break;
    case RISCC::TAIL_REG:
    case RISCC::TAIL32:
    case RISCC::TAIL_MIN:
    case RISCC::LINK_S3_TAIL_MIN:
      Link = RISCC::S0;
      break;
    case RISCC::CALL_NANO_REG:
    case RISCC::CALL_NANO:
      Link = RISCC::R6;
      TransferOpcode = RISCC::JALR_NANO;
      break;
    case RISCC::TAIL_NANO_REG:
    case RISCC::TAIL_NANO:
      Link = RISCC::R0;
      TransferOpcode = RISCC::JALR_NANO;
      break;
    case RISCC::LINK_S3_CALL_MIN:
      Link = RISCC::S3;
      break;
    case RISCC::CALL16:
    case RISCC::TAIL16:
    case RISCC::LINK_S3_CALL16:
    case RISCC::LINK_S3_TAIL16:
      EmitToStreamer(*OutStreamer,
                     MCInstBuilder(RISCC::JAL16)
                         .addReg(Opcode == RISCC::CALL16           ? RISCC::S7
                                 : Opcode == RISCC::LINK_S3_CALL16 ? RISCC::S3
                                                                   : RISCC::S0)
                         .addOperand(Out.getOperand(0)));
      return;
    }

    if (Link) {
      if (Out.getOperand(0).isReg()) {
        EmitToStreamer(*OutStreamer, MCInstBuilder(TransferOpcode)
                                         .addReg(Link)
                                         .addOperand(Out.getOperand(0)));
        return;
      }
      EmitToStreamer(*OutStreamer, MCInstBuilder(RISCC::LDI16)
                                       .addReg(RISCC::R0)
                                       .addOperand(Out.getOperand(0)));
      EmitToStreamer(
          *OutStreamer,
          MCInstBuilder(TransferOpcode).addReg(Link).addReg(RISCC::R0));
      return;
    }
    EmitToStreamer(*OutStreamer, Out);
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
    OS << '['
       << RISCCInstPrinter::getRegisterName(MI->getOperand(OpNo).getReg())
       << ']';
    return false;
  }
};
} // namespace

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
PreservedAnalyses
RISCCAsmPrinterPass::run(MachineFunction &MF,
                         MachineFunctionAnalysisManager &MFAM) {
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
