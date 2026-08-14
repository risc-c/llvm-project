//===-- RISCCConstantPoolValue.cpp - RISC-C constant-pool values ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCConstantPoolValue.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"

using namespace llvm;

RISCCConstantPoolSymbol::RISCCConstantPoolSymbol(LLVMContext &Context,
                                                 StringRef Name)
    : MachineConstantPoolValue(Type::getInt32Ty(Context)), Symbol(Name) {}

RISCCConstantPoolSymbol::RISCCConstantPoolSymbol(LLVMContext &Context,
                                                 const GlobalValue *GV,
                                                 bool IsTPOFF)
    : MachineConstantPoolValue(Type::getInt32Ty(Context)), Global(GV),
      IsTPOFF(IsTPOFF) {}

RISCCConstantPoolSymbol *RISCCConstantPoolSymbol::Create(LLVMContext &Context,
                                                          StringRef Name) {
  return new RISCCConstantPoolSymbol(Context, Name);
}

RISCCConstantPoolSymbol *RISCCConstantPoolSymbol::Create(LLVMContext &Context,
                                                          const GlobalValue *GV,
                                                          bool IsTPOFF) {
  return new RISCCConstantPoolSymbol(Context, GV, IsTPOFF);
}

int RISCCConstantPoolSymbol::getExistingMachineCPValue(
    MachineConstantPool *Pool, Align Alignment) {
  const auto &Constants = Pool->getConstants();
  for (unsigned I = 0; I != Constants.size(); ++I) {
    const MachineConstantPoolEntry &Entry = Constants[I];
    if (!Entry.isMachineConstantPoolEntry() || Entry.getAlign() < Alignment)
      continue;
    auto *Other = static_cast<RISCCConstantPoolSymbol *>(
        Entry.Val.MachineCPVal);
    if (Other->Global == Global && Other->Symbol == Symbol &&
        Other->IsTPOFF == IsTPOFF)
      return I;
  }
  return -1;
}

void RISCCConstantPoolSymbol::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  ID.AddPointer(Global);
  ID.AddString(Symbol);
  ID.AddBoolean(IsTPOFF);
}

void RISCCConstantPoolSymbol::print(raw_ostream &OS) const {
  OS << (Global ? Global->getName() : Symbol);
  if (IsTPOFF)
    OS << "@TPOFF";
}
