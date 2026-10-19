; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -enable-ipra=false %s -o - | FileCheck %s --check-prefixes=RC32,CHECK
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -enable-ipra=false %s -o - | FileCheck %s --check-prefixes=RC32,CHECK
; RUN: llc -mtriple=riscc -mcpu=full -enable-ipra=false %s -o - | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -riscc-link-literals=false -enable-ipra=false %s -o - | \
; RUN:   FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -filetype=obj -enable-ipra=false %s -o %t.o
; RUN: llvm-readobj --relocations %t.o | FileCheck %s --check-prefix=OBJ

; Private, four-byte RC32 pools can be shared after call relaxation. Speed
; functions, opaque inline assembly, and RC16 do not opt into this layout pass.
; RC32-LABEL: address:
; RC32:       .reloc [[WORD:.Ltmp[0-9]+]], R_RISCC_RELAX_LITERAL, [[WORD]]
; RC32:       ldpc r1, [[WORD]]
; RC32:       [[WORD]]:
; RC32-NEXT:  .long external
; CHECK-LABEL: speed:
; CHECK-NOT:   R_RISCC_RELAX_LITERAL
; CHECK:       .Lfunc_end
; CHECK-LABEL: opaque:
; CHECK-NOT:   R_RISCC_RELAX_LITERAL
; CHECK:       .Lfunc_end
; OFF-NOT:     R_RISCC_RELAX_LITERAL
; OBJ-COUNT-1: R_RISCC_RELAX_LITERAL
; OBJ-NOT:     R_RISCC_RELAX_LITERAL

@external = external global i32

define ptr @address() optsize {
  ret ptr @external
}

define ptr @speed() {
  ret ptr @external
}

define ptr @opaque() optsize {
  call void asm sideeffect "or r0, r0, r0", "~{r0}"()
  ret ptr @external
}
