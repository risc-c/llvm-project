//===-- RISCCMCExpr.cpp - RISCC MC Expression -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCMCExpr.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

const RISCCMCExpr *RISCCMCExpr::create(VariantKind Kind, const MCExpr *Expr,
                                       MCContext &Ctx) {
  return new (Ctx) RISCCMCExpr(Kind, Expr);
}

std::optional<RISCCMCExpr::VariantKind>
RISCCMCExpr::parseVariantKind(StringRef Name) {
  return StringSwitch<std::optional<VariantKind>>(Name.lower())
      .Case("lo8", VK_LO8)
      .Case("hi8", VK_HI8)
      .Case("code", VK_CODE)
      .Case("code_lo8", VK_CODE_LO8)
      .Case("code_hi8", VK_CODE_HI8)
      .Case("tpoff", VK_TPOFF)
      .Case("call_target", VK_CALL_TARGET)
      .Default(std::nullopt);
}

StringRef RISCCMCExpr::getVariantName(VariantKind Kind) {
  switch (Kind) {
  case VK_None:
    return "";
  case VK_LO8:
    return "lo8";
  case VK_HI8:
    return "hi8";
  case VK_CODE:
    return "code";
  case VK_CODE_LO8:
    return "code_lo8";
  case VK_CODE_HI8:
    return "code_hi8";
  case VK_TPOFF:
    return "tpoff";
  case VK_CALL_TARGET:
    return "call_target";
  }
  llvm_unreachable("invalid RISC-C expression variant");
}

void RISCCMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  if (Kind == VK_None) {
    MAI->printExpr(OS, *Expr);
    return;
  }
  OS << getVariantName(Kind) << '(';
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
