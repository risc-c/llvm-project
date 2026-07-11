#include "RISCCMCExpr.h"
#include "llvm/Support/Casting.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCValue.h"

using namespace llvm;

const RISCCMCExpr *RISCCMCExpr::create(VariantKind Kind, const MCExpr *Expr,
                                       MCContext &Ctx) {
  return new (Ctx) RISCCMCExpr(Kind, Expr);
}

void RISCCMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  static const char *Names[] = {"", "lo8", "hi8", "code", "code_lo8",
                                "code_hi8", "tpoff"};
  if (Kind == VK_None) {
    MAI->printExpr(OS, *Expr);
    return;
  }
  OS << Names[Kind] << '(';
  MAI->printExpr(OS, *Expr);
  OS << ')';
}

bool RISCCMCExpr::evaluateAsRelocatableImpl(MCValue &Res,
                                            const MCAssembler *Asm) const {
  MCValue Inner;
  if (!Expr->evaluateAsRelocatable(Inner, Asm))
    return false;
  Res = MCValue::get(Inner.getAddSym(), Inner.getSubSym(), Inner.getConstant(),
                     Kind);
  return true;
}

void RISCCMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*Expr);
}

MCFragment *RISCCMCExpr::findAssociatedFragment() const {
  return Expr->findAssociatedFragment();
}

bool RISCCMCExpr::isEqualTo(const MCExpr *Other) const {
  const auto *E = dyn_cast<RISCCMCExpr>(Other);
  return E && Kind == E->Kind && Expr == E->Expr;
}
