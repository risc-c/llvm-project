//===-- RISCCCodeGenPassBuilder.cpp - RISCC CodeGen Pass Builder ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCC.h"
#include "RISCCAsmPrinter.h"
#include "RISCCConstantIslandPass.h"
#include "RISCCSRegAllocator.h"
#include "RISCCTargetMachine.h"
#include "llvm/CodeGen/AtomicExpand.h"
#include "llvm/CodeGen/BranchRelaxation.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Passes/CodeGenPassBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Target/CGPassBuilderOption.h"

using namespace llvm;

namespace {
class RISCCCodeGenPassBuilder
    : public CodeGenPassBuilder<RISCCCodeGenPassBuilder, RISCCTargetMachine> {
  using Base = CodeGenPassBuilder<RISCCCodeGenPassBuilder, RISCCTargetMachine>;

public:
  RISCCCodeGenPassBuilder(RISCCTargetMachine &TM, const CGPassBuilderOption &O,
                          PassInstrumentationCallbacks *PIC)
      : CodeGenPassBuilder(TM, O, PIC) {}
  void addIRPasses(PassManagerWrapper &PMW) const {
    addFunctionPass(AtomicExpandPass(TM), PMW);
    Base::addIRPasses(PMW);
  }
  Error addInstSelector(PassManagerWrapper &PMW) const {
    addMachineFunctionPass(RISCCISelDAGToDAGPass(TM, getOptLevel()), PMW);
    return Error::success();
  }
  void addPostRewrite(PassManagerWrapper &PMW) const {
    addMachineFunctionPass(RISCCSRegAllocatorPass(), PMW);
  }
  void addPreEmitPass(PassManagerWrapper &PMW) const {
    addMachineFunctionPass(BranchRelaxationPass(), PMW);
  }
  void addPreEmitPass2(PassManagerWrapper &PMW) const {
    addMachineFunctionPass(RISCCConstantIslandPass(), PMW);
  }
  void addAsmPrinterBegin(PassManagerWrapper &PMW) const {
    addModulePass(RISCCAsmPrinterBeginPass(), PMW, true);
  }
  void addAsmPrinter(PassManagerWrapper &PMW) const {
    addMachineFunctionPass(RISCCAsmPrinterPass(), PMW);
  }
  void addAsmPrinterEnd(PassManagerWrapper &PMW) const {
    addModulePass(RISCCAsmPrinterEndPass(), PMW);
  }
};
} // namespace

void RISCCTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB){
#define GET_PASS_REGISTRY "RISCCPassRegistry.def"
#include "llvm/Passes/TargetPassRegistry.inc"
}

Error RISCCTargetMachine::buildCodeGenPipeline(
    ModulePassManager &MPM, ModuleAnalysisManager &MAM, raw_pwrite_stream &Out,
    raw_pwrite_stream *DwoOut, CodeGenFileType FT,
    const CGPassBuilderOption &Opt, MCContext &Ctx,
    PassInstrumentationCallbacks *PIC) {
  return RISCCCodeGenPassBuilder(*this, Opt, PIC)
      .buildPipeline(MPM, MAM, Out, DwoOut, FT, Ctx);
}
