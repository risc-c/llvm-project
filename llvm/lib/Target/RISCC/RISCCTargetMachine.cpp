//===-- RISCCTargetMachine.cpp - RISCC TargetMachine ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCTargetMachine.h"
#include "RISCC.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSRegAllocator.h"
#include "TargetInfo/RISCCTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCCTarget() {
  RegisterTargetMachine<RISCCTargetMachine> X(getTheRISCCTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeRISCCAsmPrinterPass(PR);
  initializeRISCCDAGToDAGISelLegacyPass(PR);
  initializeRISCCSRegAllocatorLegacyPass(PR);
}

static Reloc::Model effectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

static std::string computeRISCCDataLayout(const Triple &TT, StringRef FS) {
  if (FS.contains("+rc32"))
    return "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-"
           "f32:32-f64:32-a:8:32-n8:16:32-S32";
  return TT.computeDataLayout();
}

RISCCTargetMachine::RISCCTargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool)
    : CodeGenTargetMachineImpl(T, computeRISCCDataLayout(TT, FS), TT, CPU, FS,
                               Options,
                               effectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

RISCCTargetMachine::~RISCCTargetMachine() = default;

MachineFunctionInfo *RISCCTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return RISCCMachineFunctionInfo::create(Allocator, F, STI);
}

namespace {
class RISCCPassConfigImpl final : public TargetPassConfig {
public:
  RISCCPassConfigImpl(RISCCTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}
  RISCCTargetMachine &getRISCCTargetMachine() const {
    return getTM<RISCCTargetMachine>();
  }
  void addIRPasses() override {
    addPass(createAtomicExpandLegacyPass());
    TargetPassConfig::addIRPasses();
  }
  bool addInstSelector() override {
    addPass(createRISCCISelDag(getRISCCTargetMachine(), getOptLevel()));
    return false;
  }
  void addPostRewrite() override {
    addPass(createRISCCSRegAllocatorLegacyPass());
  }
  void addPreEmitPass() override { addPass(&BranchRelaxationPassID); }
};
}

TargetPassConfig *RISCCTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new RISCCPassConfigImpl(*this, PM);
}
