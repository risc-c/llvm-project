//===-- RISCCTargetMachine.h - RISCC TargetMachine --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCTARGETMACHINE_H
#define LLVM_LIB_TARGET_RISCC_RISCCTARGETMACHINE_H

#include "RISCCSubtarget.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <memory>

namespace llvm {
class RISCCTargetMachine final : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  RISCCSubtarget Subtarget;

public:
  RISCCTargetMachine(const Target &, const Triple &, StringRef CPU,
                     StringRef FS, const TargetOptions &,
                     std::optional<Reloc::Model>,
                     std::optional<CodeModel::Model>, CodeGenOptLevel,
                     bool JIT);
  ~RISCCTargetMachine() override;

  const RISCCSubtarget *getSubtargetImpl(const Function &) const override {
    return &Subtarget;
  }
  TargetPassConfig *createPassConfig(PassManagerBase &) override;
  MachineFunctionInfo *createMachineFunctionInfo(
      BumpPtrAllocator &, const Function &,
      const TargetSubtargetInfo *) const override;
  void registerPassBuilderCallbacks(PassBuilder &) override;
  Error buildCodeGenPipeline(ModulePassManager &, ModuleAnalysisManager &,
                             raw_pwrite_stream &, raw_pwrite_stream *,
                             CodeGenFileType, const CGPassBuilderOption &,
                             MCContext &,
                             PassInstrumentationCallbacks *) override;
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};
}

#endif
