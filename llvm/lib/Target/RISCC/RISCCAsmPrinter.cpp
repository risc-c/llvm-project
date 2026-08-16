//===-- RISCCAsmPrinter.cpp - RISCC Assembly Printer ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCAsmPrinter.h"
#include "RISCC.h"
#include "RISCCConstantPoolValue.h"
#include "RISCCInstrInfo.h"
#include "RISCCMCInstLower.h"
#include "RISCCSubtarget.h"
#include "TargetInfo/RISCCTargetInfo.h"
#include "MCTargetDesc/RISCCInstPrinter.h"
#include "MCTargetDesc/RISCCMCExpr.h"
#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
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
#include <algorithm>

using namespace llvm;

#define DEBUG_TYPE "riscc-asm-printer"

namespace {
struct LiteralUse {
  const MachineInstr *MI;
  unsigned CPI;
  unsigned Offset;
};

struct LiteralEntry {
  unsigned CPI;
  unsigned Offset;
  MCSymbol *Symbol = nullptr;
};

struct LiteralPool {
  unsigned Offset = 0;
  unsigned LeadingBytes = 0;
  SmallVector<LiteralEntry, 8> Forward;
  SmallVector<LiteralEntry, 8> Backward;

  bool empty() const { return Forward.empty() && Backward.empty(); }
};

class RISCCAsmPrinter final : public AsmPrinter {
public:
  static char ID;
  RISCCAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> S)
      : AsmPrinter(TM, std::move(S), ID) {}
  StringRef getPassName() const override { return "RISC-C Assembly Printer"; }
  bool runOnMachineFunction(MachineFunction &MF) override {
    SetupMachineFunction(MF);
    PendingBranchLiteral = nullptr;
    PendingBranchTarget = nullptr;
    planLiteralPools();
    emitFunctionBody();
    assert(!PendingBranchLiteral && "unterminated RISC-C long branch");
    return false;
  }
  void emitConstantPool() override {
    if (LiteralPools.front().empty())
      return;

    const Function &F = MF->getFunction();
    if (MF->front().isBeginSection())
      MF->setSection(getObjFileLowering().getUniqueSectionForFunction(F, TM));
    else
      MF->setSection(getObjFileLowering().SectionForGlobal(&F, TM));
    OutStreamer->switchSection(MF->getSection());
    emitLiteralPool(LiteralPools.front());
  }
  void emitBasicBlockEnd(const MachineBasicBlock &MBB) override {
    AsmPrinter::emitBasicBlockEnd(MBB);
    auto It = PoolAfter.find(&MBB);
    if (It != PoolAfter.end())
      emitLiteralPool(LiteralPools[It->second]);
  }
  void emitMachineConstantPoolValue(MachineConstantPoolValue *Value) override {
    auto *Symbol = static_cast<RISCCConstantPoolSymbol *>(Value);
    const MCExpr *Expr = MCSymbolRefExpr::create(
        Symbol->getGlobal() ? getSymbol(Symbol->getGlobal())
                            : GetExternalSymbolSymbol(Symbol->getSymbol()),
        OutContext);
    if (Symbol->isTPOFF())
      Expr = RISCCMCExpr::create(RISCCMCExpr::VK_TPOFF, Expr, OutContext);
    OutStreamer->emitValue(Expr, 4);
  }
  void emitLiteralLoad(const MachineInstr *MI) {
    MCSymbol *Literal = LiteralSymbols.lookup(MI);
    assert(Literal && "literal load was not assigned a pool");
    MCInst Load;
    RISCCMCInstLower(OutContext, *this).lower(MI, Load);
    Load.getOperand(1) = MCOperand::createExpr(
        MCSymbolRefExpr::create(Literal, OutContext));
    EmitToStreamer(*OutStreamer, Load);
  }
  void emitInstruction(const MachineInstr *MI) override {
    RISCC_MC::verifyInstructionPredicates(MI->getOpcode(),
                                           getSubtargetInfo().getFeatureBits());
    if (MI->getOpcode() == RISCC::LDPC && MI->getOperand(1).isCPI()) {
      emitLiteralLoad(MI);
      return;
    }
    MCInst Out;
    RISCCMCInstLower(OutContext, *this).lower(MI, Out);
    unsigned Opcode = MI->getOpcode();

    if (MF->getSubtarget<RISCCSubtarget>().isRC32() &&
        isLongBranchLoad(*MI)) {
      assert(!PendingBranchLiteral && "nested RISC-C long branch");
      PendingBranchLiteral = OutContext.createTempSymbol();
      PendingBranchTarget = Out.getOperand(1).getExpr();
      EmitToStreamer(*OutStreamer,
                     MCInstBuilder(RISCC::LDPC)
                         .addReg(Out.getOperand(0).getReg())
                         .addExpr(MCSymbolRefExpr::create(
                             PendingBranchLiteral, OutContext)));
      return;
    }

    MCRegister Link;
    unsigned TransferOpcode = RISCC::JALR;
    switch (Opcode) {
    default:
      break;
    case RISCC::RETS:
      EmitToStreamer(*OutStreamer, MCInstBuilder(RISCC::RET).addReg(RISCC::S7));
      return;
    case RISCC::RET_NANO:
      EmitToStreamer(*OutStreamer,
                     MCInstBuilder(RISCC::JALR_NANO)
                         .addReg(RISCC::R0)
                         .addOperand(Out.getOperand(0)));
      return;
    case RISCC::LINK_S3_RET:
      EmitToStreamer(*OutStreamer, MCInstBuilder(RISCC::RET).addReg(RISCC::S3));
      return;
    case RISCC::CALL:
    case RISCC::CALL32:
      Link = RISCC::S7;
      break;
    case RISCC::TAIL_REG:
    case RISCC::TAIL32:
      Link = RISCC::S0;
      break;
    case RISCC::CALL_NANO_REG:
      Link = RISCC::R6;
      TransferOpcode = RISCC::JALR_NANO;
      break;
    case RISCC::TAIL_NANO_REG:
      Link = RISCC::R0;
      TransferOpcode = RISCC::JALR_NANO;
      break;
    case RISCC::LINK_S3_CALL_MIN:
      Link = RISCC::S3;
      break;
    case RISCC::LINK_S3_TAIL_MIN:
      Link = RISCC::S0;
      break;
    case RISCC::CALL_MIN:
      Link = RISCC::S7;
      break;
    case RISCC::TAIL_MIN:
      Link = RISCC::S0;
      break;
    case RISCC::CALL_NANO:
      Link = RISCC::R6;
      TransferOpcode = RISCC::JALR_NANO;
      break;
    case RISCC::TAIL_NANO:
      Link = RISCC::R0;
      TransferOpcode = RISCC::JALR_NANO;
      break;
    case RISCC::CALL16:
    case RISCC::TAIL16:
    case RISCC::LINK_S3_CALL16:
    case RISCC::LINK_S3_TAIL16:
      EmitToStreamer(
          *OutStreamer,
          MCInstBuilder(RISCC::JAL16)
              .addReg(Opcode == RISCC::CALL16 ? RISCC::S7 :
                      Opcode == RISCC::LINK_S3_CALL16 ? RISCC::S3 : RISCC::S0)
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
      EmitToStreamer(*OutStreamer, MCInstBuilder(TransferOpcode)
                                          .addReg(Link)
                                          .addReg(RISCC::R0));
      return;
    }
    EmitToStreamer(*OutStreamer, Out);
    if (PendingBranchLiteral && Opcode == RISCC::JALR)
      emitPendingBranchLiteral();
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

private:
  static SmallVector<LiteralEntry, 8>
  collectEntries(ArrayRef<LiteralUse> Uses, bool Backward) {
    SmallVector<LiteralEntry, 8> Entries;
    for (const LiteralUse &Use : Uses) {
      auto It = llvm::find_if(Entries, [&](const LiteralEntry &Entry) {
        return Entry.CPI == Use.CPI;
      });
      if (It == Entries.end()) {
        Entries.push_back({Use.CPI, Use.Offset});
        continue;
      }
      It->Offset = Backward ? std::max(It->Offset, Use.Offset)
                             : std::min(It->Offset, Use.Offset);
    }
    llvm::sort(Entries, [](const LiteralEntry &A, const LiteralEntry &B) {
      return A.Offset < B.Offset;
    });
    return Entries;
  }

  static unsigned entryIndex(ArrayRef<LiteralEntry> Entries, unsigned CPI) {
    for (unsigned I = 0; I != Entries.size(); ++I)
      if (Entries[I].CPI == CPI)
        return I;
    llvm_unreachable("literal entry is missing");
  }

  static bool fitsForward(const LiteralPool &Pool,
                          ArrayRef<LiteralUse> Uses) {
    SmallVector<LiteralEntry, 8> Entries = collectEntries(Uses, false);
    for (const LiteralUse &Use : Uses) {
      unsigned Index = entryIndex(Entries, Use.CPI);
      // Alignment can add two bytes before a pool. This is the largest
      // positive PC-relative displacement that the pool can produce.
      int64_t Displacement = int64_t(Pool.Offset) + Pool.LeadingBytes +
                             4 * Index - Use.Offset;
      if (Displacement > 254)
        return false;
    }
    return true;
  }

  static bool fitsBackward(unsigned PoolOffset, ArrayRef<LiteralUse> Uses) {
    SmallVector<LiteralEntry, 8> Entries = collectEntries(Uses, true);
    for (const LiteralUse &Use : Uses) {
      unsigned Index = entryIndex(Entries, Use.CPI);
      int64_t Displacement = int64_t(PoolOffset) - Use.Offset - 2 -
                             4 * (Entries.size() - Index);
      if (Displacement < -256)
        return false;
    }
    return true;
  }

  void assignPoolUses(LiteralPool &Pool, ArrayRef<LiteralUse> Uses,
                      bool Backward) {
    SmallVector<LiteralEntry, 8> Entries = collectEntries(Uses, Backward);
    for (LiteralEntry &Entry : Entries)
      Entry.Symbol = OutContext.createTempSymbol();
    for (const LiteralUse &Use : Uses) {
      unsigned Index = entryIndex(Entries, Use.CPI);
      LiteralSymbols[Use.MI] = Entries[Index].Symbol;
    }
    auto &Destination = Backward ? Pool.Backward : Pool.Forward;
    Destination.append(Entries);
  }

  static bool isLongBranchLoad(const MachineInstr &MI) {
    if (MI.getOpcode() != RISCC::LDI16 || MI.getNumOperands() < 2 ||
        !MI.getOperand(1).isMBB())
      return false;
    auto Next = std::next(MI.getIterator());
    auto End = MI.getParent()->end();
    while (Next != End && Next->isDebugInstr())
      ++Next;
    return Next != End && Next->getOpcode() == RISCC::JALR;
  }

  void emitPendingBranchLiteral() {
    emitAlignment(Align(4));
    OutStreamer->emitLabel(PendingBranchLiteral);
    OutStreamer->emitValue(PendingBranchTarget, 4);
    PendingBranchLiteral = nullptr;
    PendingBranchTarget = nullptr;
  }

  void planLiteralPools() {
    LiteralSymbols.clear();
    LiteralPools.clear();
    PoolAfter.clear();
    LiteralPools.push_back({});
    unsigned LeftPool = 0;
    SmallVector<LiteralUse, 8> Uses;
    unsigned Offset = 0;
    const auto &TII = *MF->getSubtarget().getInstrInfo();
    const bool IsRC32 = MF->getSubtarget<RISCCSubtarget>().isRC32();

    // Pools are emitted before a function or after a barrier block. First
    // assign as many literals as fit in the preceding pool; the rest use this
    // gap.
    for (const MachineBasicBlock &MBB : *MF) {
      for (const MachineInstr &MI : MBB) {
        if (MI.getOpcode() == RISCC::LDPC && MI.getOperand(1).isCPI())
          Uses.push_back(
              {&MI, unsigned(MI.getOperand(1).getIndex()), Offset});
        Offset += TII.getInstSizeInBytes(MI);
      }

      auto Last = MBB.getLastNonDebugInstr();
      if (Last == MBB.end() || !Last->isBarrier())
        continue;

      unsigned RightPool = LiteralPools.size();
      LiteralPools.push_back({Offset});
      if (IsRC32) {
        for (const MachineInstr &MI : MBB)
          if (isLongBranchLoad(MI)) {
            // The long branch writes its target literal before this pool.
            // Alignment can make it six bytes long.
            LiteralPools[RightPool].LeadingBytes = 6;
            break;
          }
      }
      unsigned Split = Uses.size();
      while (Split && !fitsBackward(LiteralPools[LeftPool].Offset,
                                    ArrayRef(Uses).take_front(Split)))
        --Split;
      if (!fitsForward(LiteralPools[RightPool],
                       ArrayRef(Uses).drop_front(Split)))
        report_fatal_error("RISC-C literal has no reachable pool within "
                           "the LDPC range");

      ArrayRef<LiteralUse> Backward = ArrayRef(Uses).take_front(Split);
      ArrayRef<LiteralUse> Forward = ArrayRef(Uses).drop_front(Split);
      assignPoolUses(LiteralPools[LeftPool], Backward, true);
      assignPoolUses(LiteralPools[RightPool], Forward, false);
      PoolAfter[&MBB] = RightPool;
      LeftPool = RightPool;
      Uses.clear();
    }

    if (!Uses.empty())
      report_fatal_error("RISC-C function has no terminal literal-pool gap");
  }

  void emitLiteralPool(const LiteralPool &Pool) {
    if (Pool.empty())
      return;
    if (!Pool.LeadingBytes)
      emitAlignment(Align(4));
    const auto &Constants = MF->getConstantPool()->getConstants();
    auto EmitEntries = [&](ArrayRef<LiteralEntry> Entries) {
      for (const LiteralEntry &Entry : Entries) {
        OutStreamer->emitLabel(Entry.Symbol);
        const MachineConstantPoolEntry &Constant = Constants[Entry.CPI];
        if (Constant.isMachineConstantPoolEntry())
          emitMachineConstantPoolValue(Constant.Val.MachineCPVal);
        else
          emitGlobalConstant(getDataLayout(), Constant.Val.ConstVal);
      }
    };
    EmitEntries(Pool.Forward);
    EmitEntries(Pool.Backward);
  }

  SmallVector<LiteralPool, 8> LiteralPools;
  DenseMap<const MachineBasicBlock *, unsigned> PoolAfter;
  DenseMap<const MachineInstr *, MCSymbol *> LiteralSymbols;
  MCSymbol *PendingBranchLiteral = nullptr;
  const MCExpr *PendingBranchTarget = nullptr;
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
