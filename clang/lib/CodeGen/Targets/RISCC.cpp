//===- RISCC.cpp - RISC-C ABI implementation -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {

class RISCCABIInfo : public DefaultABIInfo {
  static constexpr unsigned ReturnSlots = 4;
  static constexpr unsigned SlotBits = 16;

  ABIArgInfo coerceAggregateToSlots(QualType Ty) const {
    const unsigned Size = getContext().getTypeSize(Ty);
    if (Size == 0 || isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();

    // Present aggregates to LLVM as a single little-endian integer whose
    // width is an integral number of ABI slots.  Legalization then splits it
    // into low-word-first i16 parts carrying one OrigArgIndex, so the backend
    // can keep the entire argument in registers or put the entire argument on
    // the stack.  Padding the final partial word also makes byte-field
    // records such as {u8, u8} consume one slot rather than two.
    llvm::Type *CoerceTy = llvm::IntegerType::get(
        getVMContext(), llvm::alignTo(Size, SlotBits));
    return ABIArgInfo::getDirect(CoerceTy);
  }

  ABIArgInfo classifyReturnType(QualType Ty) const {
    if (Ty->isVoidType())
      return ABIArgInfo::getIgnore();

    const unsigned Size = getContext().getTypeSize(Ty);
    if (Size > ReturnSlots * SlotBits)
      return getNaturalAlignIndirect(Ty,
                                     getDataLayout().getAllocaAddrSpace());

    // Small aggregates are returned as low-word-first 16-bit slots in
    // r1..r4.  Explicit coercion is important: leaving the source aggregate
    // direct would flatten {u8, u8} into two byte values and consume two
    // registers.
    if (isAggregateTypeForABI(Ty))
      return coerceAggregateToSlots(Ty);

    if (isPromotableIntegerTypeForABI(Ty))
      return ABIArgInfo::getExtend(Ty);
    return ABIArgInfo::getDirect();
  }

  ABIArgInfo classifyArgumentType(QualType Ty) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);
    const unsigned Size = getContext().getTypeSize(Ty);

    // The ABI promotes byte-sized integer arguments to one 16-bit slot.
    if (Ty->isIntegralOrEnumerationType() && Size <= 8)
      return ABIArgInfo::getExtend(Ty);

    // C aggregates are passed by value, either wholly in r1..r4 or wholly on
    // the stack, with a partial final word padded to one full slot.  Indirect
    // arguments are reserved for a future C++ ABI.
    if (isAggregateTypeForABI(Ty))
      return coerceAggregateToSlots(Ty);
    return ABIArgInfo::getDirect();
  }

public:
  explicit RISCCABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

    for (auto &Arg : FI.arguments())
      Arg.info = classifyArgumentType(Arg.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*IsIndirect=*/false,
                            getContext().getTypeInfoInChars(Ty),
                            CharUnits::fromQuantity(SlotBits / 8),
                            /*AllowHigherAlign=*/false, Slot);
  }
};

class RISCCTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  explicit RISCCTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<RISCCABIInfo>(CGT)) {}
};

} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createRISCCTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<RISCCTargetCodeGenInfo>(CGM.getTypes());
}
