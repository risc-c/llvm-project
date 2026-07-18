//===-- RISCC.h - Top-level interface for RISCC -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCC_H
#define LLVM_LIB_TARGET_RISCC_RISCC_H

#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {
class FunctionPass;
class Function;
class RISCCTargetMachine;
class PassRegistry;

class RISCCISelDAGToDAGPass : public SelectionDAGISelPass {
public:
  RISCCISelDAGToDAGPass(RISCCTargetMachine &, CodeGenOptLevel);
};

FunctionPass *createRISCCISelDag(RISCCTargetMachine &, CodeGenOptLevel);
MCRegister getRISCCMainlineLinkRegister(const Function &);
void initializeRISCCDAGToDAGISelLegacyPass(PassRegistry &);
void initializeRISCCAsmPrinterPass(PassRegistry &);

namespace RISCCII {
enum TOF : unsigned {
  MO_None,
  MO_CODE,
  MO_TPOFF,

  // SelectionDAG-only marker for compiler-private helpers that preserve the
  // software-managed S-register cache. Call lowering replaces this with
  // MO_CODE before instruction selection.
  MO_SREG_PRESERVING_CALL
};
}
}

#endif
