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
  static constexpr unsigned ReturnSlots = 3;

  unsigned getSlotBits() const {
    return getDataLayout().getPointerSizeInBits();
  }

  ABIArgInfo coerceAggregateToSlots(QualType Ty) const {
    const unsigned Size = getContext().getTypeSize(Ty);
    if (Size == 0 || isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();

    // One integer keeps all legalized parts under the same OrigArgIndex, so
    // argument lowering can assign the whole aggregate to registers or the
    // stack. Round up to a native slot to include trailing padding.
    llvm::Type *CoerceTy = llvm::IntegerType::get(
        getVMContext(), llvm::alignTo(Size, getSlotBits()));
    return ABIArgInfo::getDirect(CoerceTy);
  }

  ABIArgInfo classifyReturnType(QualType Ty) const {
    if (Ty->isVoidType())
      return ABIArgInfo::getIgnore();

    const unsigned Size = getContext().getTypeSize(Ty);
    if (Size > ReturnSlots * getSlotBits())
      return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace());

    // Small aggregates are returned as low-slot-first native slots in r1..r3.
    // Explicit coercion avoids flattening {u8, u8} into two byte values.
    if (isAggregateTypeForABI(Ty))
      return coerceAggregateToSlots(Ty);

    if (isPromotableIntegerTypeForABI(Ty))
      return ABIArgInfo::getExtend(Ty);
    return ABIArgInfo::getDirect();
  }

  ABIArgInfo classifyArgumentType(QualType Ty) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);

    // Narrow integer arguments are extended to one native slot.
    if (isPromotableIntegerTypeForABI(Ty))
      return ABIArgInfo::getExtend(Ty);

    // C aggregates are passed by value, either wholly in r1..r3 or wholly on
    // the stack, with a partial final word padded to one full slot.
    if (isAggregateTypeForABI(Ty)) {
      if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
        return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                       RAA == CGCXXABI::RAA_DirectInMemory);
      return coerceAggregateToSlots(Ty);
    }
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
                            CharUnits::fromQuantity(getSlotBits() / 8),
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
