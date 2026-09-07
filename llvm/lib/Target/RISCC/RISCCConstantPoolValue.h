//===-- RISCCConstantPoolValue.h - RISC-C constant-pool values -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCCONSTANTPOOLVALUE_H
#define LLVM_LIB_TARGET_RISCC_RISCCCONSTANTPOOLVALUE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include <string>

namespace llvm {

class LLVMContext;
class GlobalValue;

class RISCCConstantPoolSymbol final : public MachineConstantPoolValue {
  const GlobalValue *Global = nullptr;
  std::string Symbol;
  bool IsTPOFF = false;
  bool IsCallTarget = false;

  RISCCConstantPoolSymbol(LLVMContext &, StringRef);
  RISCCConstantPoolSymbol(LLVMContext &, const GlobalValue *, bool, bool);

public:
  static RISCCConstantPoolSymbol *Create(LLVMContext &, StringRef);
  static RISCCConstantPoolSymbol *Create(LLVMContext &, const GlobalValue *,
                                         bool IsTPOFF,
                                         bool IsCallTarget = false);
  static RISCCConstantPoolSymbol *CreateCallTarget(LLVMContext &, StringRef);

  const GlobalValue *getGlobal() const { return Global; }
  StringRef getSymbol() const { return Symbol; }
  bool isTPOFF() const { return IsTPOFF; }
  bool isCallTarget() const { return IsCallTarget; }
  int getExistingMachineCPValue(MachineConstantPool *, Align) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &) override;
  void print(raw_ostream &) const override;
};

} // namespace llvm

#endif
