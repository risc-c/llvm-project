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
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

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
    SetupMachineFunction(MF); emitFunctionBody(); return false;
  }
  void emitInstruction(const MachineInstr *MI) override {
    RISCC_MC::verifyInstructionPredicates(MI->getOpcode(),
                                           getSubtargetInfo().getFeatureBits());
    MCInst Out;
    RISCCMCInstLower(OutContext, *this).lower(MI, Out);
    if (MI->getOpcode() == RISCC::LONG_BR) {
      // LONG_BR is a post-layout six-byte macro.  Emit its two real
      // instructions here so both object output and textual -S output are
      // valid; an empty-AsmString pseudo cannot be handled by InstPrinter.
      unsigned ShortOpc = MI->getOperand(0).getImm();
      unsigned Inverse = ShortOpc == RISCC::BEQZ ? RISCC::BNEZ
                       : ShortOpc == RISCC::BNEZ ? RISCC::BEQZ
                       : ShortOpc == RISCC::BLTZ ? RISCC::BGEZ
                                                : RISCC::BLTZ;
      MCInst Skip;
      Skip.setOpcode(Inverse);
      Skip.addOperand(MCOperand::createImm(2)); // Skip the four-byte JMP16.
      EmitToStreamer(*OutStreamer, Skip);

      MCInst Jump;
      Jump.setOpcode(RISCC::JMP16);
      Jump.addOperand(Out.getOperand(1));
      EmitToStreamer(*OutStreamer, Jump);
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
    if (MO.isReg()) OS << RISCCInstPrinter::getRegisterName(MO.getReg());
    else if (MO.isImm()) OS << MO.getImm();
    else return true;
    return false;
  }
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override {
    if (ExtraCode && ExtraCode[0]) return true;
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
  setupModuleAsmPrinter(M, MAM, AP); AP.doInitialization(M);
  return PreservedAnalyses::all();
}
PreservedAnalyses RISCCAsmPrinterPass::run(
    MachineFunction &MF, MachineFunctionAnalysisManager &MFAM) {
  auto &AP = static_cast<RISCCAsmPrinter &>(
      MFAM.getResult<ModuleAnalysisManagerMachineFunctionProxy>(MF)
          .getCachedResult<AsmPrinterAnalysis>(*MF.getFunction().getParent())
          ->getPrinter());
  setupMachineFunctionAsmPrinter(MFAM, MF, AP); AP.runOnMachineFunction(MF);
  return PreservedAnalyses::all();
}
PreservedAnalyses RISCCAsmPrinterEndPass::run(Module &M,
                                              ModuleAnalysisManager &MAM) {
  auto &AP = static_cast<RISCCAsmPrinter &>(
      MAM.getResult<AsmPrinterAnalysis>(M).getPrinter());
  setupModuleAsmPrinter(M, MAM, AP); AP.doFinalization(M);
  return PreservedAnalyses::all();
}
