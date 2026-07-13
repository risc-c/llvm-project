#include "RISCCMCInstLower.h"
#include "RISCC.h"
#include "MCTargetDesc/RISCCMCExpr.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

MCOperand RISCCMCInstLower::lowerSymbolOperand(const MachineOperand &MO,
                                               MCSymbol *Sym) const {
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
  if (!MO.isJTI() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);

  RISCCMCExpr::VariantKind Kind = RISCCMCExpr::VK_None;
  switch (MO.getTargetFlags()) {
  case RISCCII::MO_None:
    break;
  case RISCCII::MO_LO8:
    Kind = RISCCMCExpr::VK_LO8;
    break;
  case RISCCII::MO_HI8:
    Kind = RISCCMCExpr::VK_HI8;
    break;
  case RISCCII::MO_CODE:
    Kind = RISCCMCExpr::VK_CODE;
    break;
  case RISCCII::MO_CODE_LO8:
    Kind = RISCCMCExpr::VK_CODE_LO8;
    break;
  case RISCCII::MO_CODE_HI8:
    Kind = RISCCMCExpr::VK_CODE_HI8;
    break;
  case RISCCII::MO_TPOFF:
    Kind = RISCCMCExpr::VK_TPOFF;
    break;
  default:
    llvm_unreachable("unknown RISC-C target flag");
  }
  if (Kind != RISCCMCExpr::VK_None)
    Expr = RISCCMCExpr::create(Kind, Expr, Ctx);
  return MCOperand::createExpr(Expr);
}

void RISCCMCInstLower::lower(const MachineInstr *MI, MCInst &Out) const {
  Out.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isReg() && MO.isImplicit())
      continue;
    switch (MO.getType()) {
    case MachineOperand::MO_Register:
      Out.addOperand(MCOperand::createReg(MO.getReg()));
      break;
    case MachineOperand::MO_Immediate:
      Out.addOperand(MCOperand::createImm(MO.getImm()));
      break;
    case MachineOperand::MO_MachineBasicBlock:
      Out.addOperand(MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx)));
      break;
    case MachineOperand::MO_GlobalAddress:
      Out.addOperand(lowerSymbolOperand(MO, Printer.getSymbol(MO.getGlobal())));
      break;
    case MachineOperand::MO_ExternalSymbol:
      Out.addOperand(lowerSymbolOperand(
          MO, Printer.GetExternalSymbolSymbol(MO.getSymbolName())));
      break;
    case MachineOperand::MO_BlockAddress:
      Out.addOperand(lowerSymbolOperand(
          MO, Printer.GetBlockAddressSymbol(MO.getBlockAddress())));
      break;
    case MachineOperand::MO_ConstantPoolIndex:
      Out.addOperand(lowerSymbolOperand(
          MO, Printer.GetCPISymbol(MO.getIndex())));
      break;
    case MachineOperand::MO_JumpTableIndex:
      Out.addOperand(lowerSymbolOperand(
          MO, Printer.GetJTISymbol(MO.getIndex())));
      break;
    case MachineOperand::MO_RegisterMask:
    case MachineOperand::MO_RegisterLiveOut:
      break;
    default:
      MI->print(errs());
      llvm_unreachable("unsupported RISC-C machine operand");
    }
  }
}
