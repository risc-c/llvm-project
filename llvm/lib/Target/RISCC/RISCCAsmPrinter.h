#ifndef LLVM_LIB_TARGET_RISCC_RISCCASMPRINTER_H
#define LLVM_LIB_TARGET_RISCC_RISCCASMPRINTER_H

#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class RISCCAsmPrinterBeginPass : public RequiredPassInfoMixin<RISCCAsmPrinterBeginPass> {
public: PreservedAnalyses run(Module &, ModuleAnalysisManager &);
};
class RISCCAsmPrinterPass : public RequiredPassInfoMixin<RISCCAsmPrinterPass> {
public: PreservedAnalyses run(MachineFunction &, MachineFunctionAnalysisManager &);
};
class RISCCAsmPrinterEndPass : public RequiredPassInfoMixin<RISCCAsmPrinterEndPass> {
public: PreservedAnalyses run(Module &, ModuleAnalysisManager &);
};
}

#endif
