//===-- RISCCMCExpr.h - RISCC MC Expression ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCMCEXPR_H
#define LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCMCEXPR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCExpr.h"
#include <optional>

namespace llvm {
class RISCCMCExpr final : public MCTargetExpr {
public:
  enum VariantKind {
    VK_None,
    VK_LO8,
    VK_HI8,
    VK_CODE,
    VK_CODE_LO8,
    VK_CODE_HI8,
    VK_TPOFF,
  };

  static const RISCCMCExpr *create(VariantKind Kind, const MCExpr *Expr,
                                   MCContext &Ctx);
  static std::optional<VariantKind> parseVariantKind(StringRef Name);
  static StringRef getVariantName(VariantKind Kind);
  VariantKind getKind() const { return Kind; }
  const MCExpr *getSubExpr() const { return Expr; }

  void printImpl(raw_ostream &, const MCAsmInfo *) const override;
  bool evaluateAsRelocatableImpl(MCValue &, const MCAssembler *) const override;
  void visitUsedExpr(MCStreamer &) const override;
  MCFragment *findAssociatedFragment() const override;
  bool isEqualTo(const MCExpr *Other) const override;

  static bool classof(const MCExpr *E) { return E->getKind() == MCExpr::Target; }

private:
  RISCCMCExpr(VariantKind Kind, const MCExpr *Expr) : Kind(Kind), Expr(Expr) {}
  VariantKind Kind;
  const MCExpr *Expr;
};
}

#endif
