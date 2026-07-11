#ifndef LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCMCASMINFO_H
#define LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;
class RISCCMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;
public:
  RISCCMCAsmInfo(const Triple &, const MCTargetOptions &);
};
}

#endif
