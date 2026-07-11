#ifndef LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCFIXUPKINDS_H
#define LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm::RISCC {
enum Fixups {
  fixup_abs8 = FirstTargetFixupKind,
  fixup_abs16,
  fixup_abs32,
  fixup_lo8,
  fixup_hi8,
  fixup_code16,
  fixup_code_lo8,
  fixup_code_hi8,
  fixup_pcrel8_word,
  fixup_tpoff_lo8,
  fixup_tpoff_hi8,
  fixup_insn_align,
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
}

#endif
