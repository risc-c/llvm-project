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
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "riscc-asm-printer"

namespace {
static cl::opt<bool> EnableLinkLiteralPools(
    "riscc-link-literals", cl::Hidden, cl::init(true),
    cl::desc("Mark private RISC-C literal words for link-time sharing"));

class RISCCAsmPrinter final : public AsmPrinter {
  SmallPtrSet<const MachineInstr *, 16> RelaxableBranches;
  SmallPtrSet<const MCSymbol *, 16> BranchLiterals;

  void emitLiteralPoolMarkers() {
    if (!EnableLinkLiteralPools ||
        !MF->getSubtarget<RISCCSubtarget>().isRC32() ||
        !MF->getFunction().hasOptSize() || MF->hasBBSections())
      return;
    // A private-word marker promises that code/data references are relocatable
    // and that removing four bytes cannot break a stronger alignment contract.
    for (const MachineBasicBlock &MBB : *MF) {
      if (MBB.getAlignment() > Align(4))
        return;
      for (const MachineInstr &MI : MBB)
        if (MI.isBundle() ||
            (MI.isInlineAsm() && MI.getOperand(0).getSymbolName()[0] != '\0'))
          return;
    }
    for (const MachineBasicBlock &MBB : *MF)
      for (const MachineInstr &MI : MBB) {
        if (MI.getOpcode() != RISCC::CONSTPOOL_ENTRY ||
            !MI.getOperand(1).isCPI())
          continue;
        const auto &Entry =
            MF->getConstantPool()->getConstants()[MI.getOperand(1).getIndex()];
        if (Entry.isMachineConstantPoolEntry() &&
            static_cast<RISCCConstantPoolSymbol *>(Entry.Val.MachineCPVal)
                ->isCallTarget())
          continue;
        if (!Entry.isMachineConstantPoolEntry() &&
            getDataLayout().getTypeAllocSize(Entry.Val.ConstVal->getType()) !=
                4)
          continue;
        const MCExpr *Word =
            MCSymbolRefExpr::create(MI.getOperand(0).getMCSymbol(), OutContext);
        OutStreamer->emitRelocDirective(*Word, "R_RISCC_RELAX_LITERAL", Word);
      }
  }

public:
  static char ID;
  RISCCAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> S)
      : AsmPrinter(TM, std::move(S), ID) {}
  StringRef getPassName() const override { return "RISC-C Assembly Printer"; }
  void emitConstantPool() override {}
  void emitFunctionBodyStart() override {
    emitLiteralPoolMarkers();
    RelaxableBranches.clear();
    BranchLiterals.clear();
    const auto &STI = MF->getSubtarget<RISCCSubtarget>();
    if (!STI.isRC32() || !STI.hasLongJall() ||
        !MF->getProperties().hasTracksLiveness())
      return;

    // A local far jump has the same range-checked linker relaxation as a
    // sibling call. Keep the full-range sequence in the object and let LLD
    // decide whether its final address fits JMPL. A shared word is safe only
    // when every reference is a marked transfer whose scratch is dispensable.
    DenseMap<const MCSymbol *, unsigned> Uses;
    DenseMap<const MCSymbol *, unsigned> BranchUses;
    DenseMap<const MCSymbol *, const MachineBasicBlock *> LocalTargets;
    SmallVector<const MachineInstr *, 16> Candidates;
    for (const MachineBasicBlock &MBB : *MF)
      for (const MachineInstr &MI : MBB) {
        if (MI.getOpcode() == RISCC::CONSTPOOL_ENTRY) {
          if (MI.getOperand(1).isMBB())
            LocalTargets[MI.getOperand(0).getMCSymbol()] =
                MI.getOperand(1).getMBB();
          continue;
        }
        for (const MachineOperand &MO : MI.operands())
          if (MO.isMCSymbol())
            ++Uses[MO.getMCSymbol()];
        if (MI.getOpcode() != RISCC::LDPC || !MI.getOperand(1).isMCSymbol())
          continue;
        Register Scratch = MI.getOperand(0).getReg();
        if (!RISCC::GPR32RegClass.contains(Scratch) || Scratch == RISCC::R7)
          continue;
        auto Next = std::next(MI.getIterator());
        if (Next != MBB.end() && Next->getOpcode() == RISCC::JALR &&
            Next->getOperand(0).getReg() == RISCC::S0 &&
            Next->getOperand(1).getReg() == Scratch) {
          Candidates.push_back(&MI);
        }
      }
    // A load whose result remains live at the destination is an ordinary
    // value use, even when it also supplies the jump's target address.
    for (const MachineInstr *MI : Candidates) {
      const MCSymbol *Literal = MI->getOperand(1).getMCSymbol();
      if (const MachineBasicBlock *Target = LocalTargets.lookup(Literal);
          Target && !Target->isLiveIn(MI->getOperand(0).getReg()))
        ++BranchUses[Literal];
    }
    for (const MachineInstr *MI : Candidates) {
      const MCSymbol *Literal = MI->getOperand(1).getMCSymbol();
      if (Uses.lookup(Literal) == BranchUses.lookup(Literal)) {
        RelaxableBranches.insert(MI);
        BranchLiterals.insert(Literal);
      }
    }
  }
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
                                      Link == RISCC::S0 ? "R_RISCC_RELAX_TAIL"
                                                        : "R_RISCC_RELAX_CALL",
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
        const MCExpr *Target =
            MCSymbolRefExpr::create(Value.getMBB()->getSymbol(), OutContext);
        if (BranchLiterals.contains(MI->getOperand(0).getMCSymbol()))
          Target = RISCCMCExpr::create(RISCCMCExpr::VK_CALL_TARGET, Target,
                                       OutContext);
        OutStreamer->emitValue(Target, 4);
      }
      return;
    }
    if (MI->getOpcode() == RISCC::CALL32_LITERAL ||
        MI->getOpcode() == RISCC::TAIL32_LITERAL ||
        MI->getOpcode() == RISCC::LINK_S3_CALL32_LITERAL ||
        MI->getOpcode() == RISCC::LINK_S3_TAIL32_LITERAL) {
      RISCC_MC::verifyInstructionPredicates(
          MI->getOpcode(), getSubtargetInfo().getFeatureBits());
      emitLiteralCall(MI, MI->getOpcode() == RISCC::CALL32_LITERAL ? RISCC::S7
                          : MI->getOpcode() == RISCC::LINK_S3_CALL32_LITERAL
                              ? RISCC::S3
                              : RISCC::S0);
      return;
    }
    if (RelaxableBranches.contains(MI)) {
      MCSymbol *Site = OutContext.createTempSymbol();
      const MCExpr *SiteExpr = MCSymbolRefExpr::create(Site, OutContext);
      const MCExpr *LiteralExpr =
          MCSymbolRefExpr::create(MI->getOperand(1).getMCSymbol(), OutContext);
      OutStreamer->emitRelocDirective(*SiteExpr, "R_RISCC_RELAX_TAIL",
                                      LiteralExpr);
      OutStreamer->emitLabel(Site);
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
    case RISCC::RET_REG:
      EmitToStreamer(*OutStreamer, MCInstBuilder(RISCC::JALR)
                                       .addReg(RISCC::S0)
                                       .addOperand(Out.getOperand(0)));
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
