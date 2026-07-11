#include "RISCCMCAsmInfo.h"

using namespace llvm;

void RISCCMCAsmInfo::anchor() {}

RISCCMCAsmInfo::RISCCMCAsmInfo(const Triple &, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 2;
  CalleeSaveStackSlotSize = 2;
  MinInstAlignment = 2;
  MaxInstLength = 6;
  CommentString = ";";
  AlignmentIsInBytes = true;
  UsesELFSectionDirectiveForBSS = true;
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::None;
}
