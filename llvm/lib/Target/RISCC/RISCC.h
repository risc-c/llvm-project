#ifndef LLVM_LIB_TARGET_RISCC_RISCC_H
#define LLVM_LIB_TARGET_RISCC_RISCC_H

#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {
class FunctionPass;
class RISCCTargetMachine;
class PassRegistry;

class RISCCISelDAGToDAGPass : public SelectionDAGISelPass {
public:
  RISCCISelDAGToDAGPass(RISCCTargetMachine &, CodeGenOptLevel);
};

class RISCCBranchSelectorPass : public PassInfoMixin<RISCCBranchSelectorPass> {
public:
  PreservedAnalyses run(MachineFunction &, MachineFunctionAnalysisManager &);
};

FunctionPass *createRISCCISelDag(RISCCTargetMachine &, CodeGenOptLevel);
FunctionPass *createRISCCBranchSelectorPass();
void initializeRISCCDAGToDAGISelLegacyPass(PassRegistry &);
void initializeRISCCBranchSelectorLegacyPass(PassRegistry &);
void initializeRISCCAsmPrinterPass(PassRegistry &);

namespace RISCCII {
enum TOF : unsigned {
  MO_None,
  MO_LO8,
  MO_HI8,
  MO_CODE,
  MO_CODE_LO8,
  MO_CODE_HI8,
  MO_TPOFF
};
}
}

#endif
