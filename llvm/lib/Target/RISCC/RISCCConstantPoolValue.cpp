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
                                                 bool IsTPOFF,
                                                 bool IsCallTarget)
    : MachineConstantPoolValue(Type::getInt32Ty(Context)), Global(GV),
      IsTPOFF(IsTPOFF), IsCallTarget(IsCallTarget) {}

RISCCConstantPoolSymbol *RISCCConstantPoolSymbol::Create(LLVMContext &Context,
                                                         StringRef Name) {
  return new RISCCConstantPoolSymbol(Context, Name);
}

RISCCConstantPoolSymbol *RISCCConstantPoolSymbol::Create(LLVMContext &Context,
                                                         const GlobalValue *GV,
                                                         bool IsTPOFF,
                                                         bool IsCallTarget) {
  return new RISCCConstantPoolSymbol(Context, GV, IsTPOFF, IsCallTarget);
}

RISCCConstantPoolSymbol *
RISCCConstantPoolSymbol::CreateCallTarget(LLVMContext &Context,
                                          StringRef Name) {
  auto *Value = new RISCCConstantPoolSymbol(Context, Name);
  Value->IsCallTarget = true;
  return Value;
}

int RISCCConstantPoolSymbol::getExistingMachineCPValue(
    MachineConstantPool *Pool, Align Alignment) {
  // A relaxable call owns its pool word so the linker can delete that exact
  // word when it rewrites the call to JALL/JMPL.
  if (IsCallTarget) {
    for (unsigned I = 0; I != Pool->getConstants().size(); ++I) {
      const MachineConstantPoolEntry &Entry = Pool->getConstants()[I];
      if (Entry.isMachineConstantPoolEntry() && Entry.Val.MachineCPVal == this)
        return I;
    }
    return -1;
  }
  const auto &Constants = Pool->getConstants();
  for (unsigned I = 0; I != Constants.size(); ++I) {
    const MachineConstantPoolEntry &Entry = Constants[I];
    if (!Entry.isMachineConstantPoolEntry() || Entry.getAlign() < Alignment)
      continue;
    auto *Other =
        static_cast<RISCCConstantPoolSymbol *>(Entry.Val.MachineCPVal);
    if (!Other->IsCallTarget && Other->Global == Global &&
        Other->Symbol == Symbol && Other->IsTPOFF == IsTPOFF)
      return I;
  }
  return -1;
}

void RISCCConstantPoolSymbol::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  if (IsCallTarget)
    ID.AddPointer(this);
  ID.AddPointer(Global);
  ID.AddString(Symbol);
  ID.AddBoolean(IsTPOFF);
  ID.AddBoolean(IsCallTarget);
}

void RISCCConstantPoolSymbol::print(raw_ostream &OS) const {
  OS << (Global ? Global->getName() : Symbol);
  if (IsTPOFF)
    OS << "@TPOFF";
  if (IsCallTarget)
    OS << "@CALL_TARGET";
}
