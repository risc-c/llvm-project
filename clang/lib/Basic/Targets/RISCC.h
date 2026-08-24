//===--- RISCC.h - Declare RISC-C target feature support -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_RISCC_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_RISCC_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY RISCCTargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];
  static const TargetInfo::GCCRegAlias GCCRegAliases[];
  std::string CPU = "full";
  bool HasMdu = false;
  bool IsRC32 = false;

  void setDataModel(bool RC32) {
    IsRC32 = RC32;
    ShortWidth = ShortAlign = 16;
    IntWidth = IntAlign = RC32 ? 32 : 16;
    LongWidth = 32;
    LongAlign = RC32 ? 32 : 16;
    LongLongWidth = 64;
    LongLongAlign = RC32 ? 32 : 16;

    HalfWidth = HalfAlign = 16;
    FloatWidth = 32;
    FloatAlign = RC32 ? 32 : 16;
    DoubleWidth = LongDoubleWidth = 64;
    DoubleAlign = LongDoubleAlign = RC32 ? 32 : 16;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();

    PointerWidth = PointerAlign = RC32 ? 32 : 16;
    SuitableAlign = RC32 ? 32 : 16;
    DefaultAlignForAttributeAligned = RC32 ? 32 : 16;

    SizeType = UnsignedInt;
    PtrDiffType = SignedInt;
    IntPtrType = SignedInt;
    IntMaxType = SignedLongLong;
    SigAtomicType = SignedInt;
    WIntType = SignedInt;
    Char16Type = RC32 ? UnsignedShort : UnsignedInt;
    Char32Type = RC32 ? UnsignedInt : UnsignedLong;
    Int16Type = RC32 ? SignedShort : SignedInt;

    resetDataLayout(RC32
                        ? "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-"
                          "f32:32-f64:32-a:8:32-n8:16:32-S32"
                        : "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-"
                          "f32:16-f64:16-a:8:16-n8:16-S16");
  }

public:
  RISCCTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    // Static local-exec TLS loads the current context from runtime state. The
    // backend rejects every dynamic TLS model.
    TLSSupported = true;
    VLASupported = false;

    MaxAtomicPromoteWidth = 0;
    MaxAtomicInlineWidth = 0;
    setDataModel(false);
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  bool allowsLargerPreferedTypeAlignment() const override { return false; }

  bool hasFeature(StringRef Feature) const override;
  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;
  bool isValidCPUName(StringRef Name) const override;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;
  bool setCPU(StringRef Name) override;

  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    if (*Name == 'r') {
      Info.setAllowsRegister();
      return true;
    }
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  bool hasBitIntType() const override { return true; }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_RISCC_H
