//===-- RISCCTargetMachine.cpp - RISCC TargetMachine ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCTargetMachine.h"
#include "MCTargetDesc/RISCCMCAsmInfo.h"
#include "RISCC.h"
#include "RISCCConstantIslandPass.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSRegAllocator.h"
#include "TargetInfo/RISCCTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/SubtargetFeature.h"

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCCTarget() {
  RegisterTargetMachine<RISCCTargetMachine> X(getTheRISCCTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeRISCCAsmPrinterPass(PR);
  initializeRISCCConstantIslandLegacyPass(PR);
  initializeRISCCDAGToDAGISelLegacyPass(PR);
  initializeRISCCSRegAllocatorLegacyPass(PR);
}

static std::string computeRISCCDataLayout(const Triple &TT, StringRef FS) {
  SubtargetFeatures Features(FS);
  bool IsRC32 = false;
  for (StringRef Feature : Features.getFeatures()) {
    if (Feature == "+rc32")
      IsRC32 = true;
    else if (Feature == "-rc32")
      IsRC32 = false;
  }
  if (IsRC32)
    return "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-"
           "f32:32-f64:32-a:8:32-n8:16:32-S32";
  return TT.computeDataLayout();
}

RISCCTargetMachine::RISCCTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool)
    : CodeGenTargetMachineImpl(T, computeRISCCDataLayout(TT, FS), TT, CPU, FS,
                               Options, RM.value_or(Reloc::Static),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  if (getRelocationModel() != Reloc::Static)
    reportFatalUsageError("RISC-C supports only static relocation");
  if (getCodeModel() != CodeModel::Small)
    reportFatalUsageError("RISC-C supports only the small code model");
  initAsmInfo();
  auto *MAI =
      static_cast<RISCCMCAsmInfo *>(const_cast<MCAsmInfo *>(AsmInfo.get()));
  MAI->setRC32(Subtarget.isRC32());
}

RISCCTargetMachine::~RISCCTargetMachine() = default;

const RISCCSubtarget *
RISCCTargetMachine::getSubtargetImpl(const Function &F) const {
  StringRef CPU = getTargetCPU();
  StringRef FS = getTargetFeatureString();
  if (Attribute Attr = F.getFnAttribute("target-cpu"); Attr.isValid())
    CPU = Attr.getValueAsString();
  if (Attribute Attr = F.getFnAttribute("target-features"); Attr.isValid())
    FS = Attr.getValueAsString();
  if (CPU == getTargetCPU() && FS == getTargetFeatureString())
    return &Subtarget;

  auto &ST = SubtargetMap[(CPU + "|" + FS).str()];
  if (!ST) {
    ST = std::make_unique<RISCCSubtarget>(getTargetTriple(), CPU.str(),
                                          FS.str(), *this);
    // Data layout and ELF flags describe one ABI for the whole module.
    if (ST->isRC32() != Subtarget.isRC32() ||
        ST->isNano() != Subtarget.isNano())
      report_fatal_error(
          "RISC-C function target attributes cannot change the ABI");
  }
  return ST.get();
}

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
  void addPreEmitPass2() override { addPass(createRISCCConstantIslandPass()); }
};
} // namespace

TargetPassConfig *RISCCTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new RISCCPassConfigImpl(*this, PM);
}
