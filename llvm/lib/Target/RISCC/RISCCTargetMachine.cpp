#include "RISCCTargetMachine.h"
#include "RISCC.h"
#include "RISCCMachineFunctionInfo.h"
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
  initializeRISCCBranchSelectorLegacyPass(PR);
}

static Reloc::Model effectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

RISCCTargetMachine::RISCCTargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
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
  void addPreEmitPass() override { addPass(createRISCCBranchSelectorPass()); }
};
}

TargetPassConfig *RISCCTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new RISCCPassConfigImpl(*this, PM);
}
