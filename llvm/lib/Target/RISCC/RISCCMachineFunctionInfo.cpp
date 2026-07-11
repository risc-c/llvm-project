#include "RISCCMachineFunctionInfo.h"

using namespace llvm;

void RISCCMachineFunctionInfo::anchor() {}

MachineFunctionInfo *RISCCMachineFunctionInfo::create(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) {
  return new (Allocator.Allocate<RISCCMachineFunctionInfo>())
      RISCCMachineFunctionInfo(F, STI);
}
