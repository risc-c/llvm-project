; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: llvm-mc -triple=riscc -mcpu=full -filetype=obj %t.s -o %t.asm.o
; RUN: llc -mtriple=riscc -mcpu=full -filetype=obj %s -o %t.direct.o
; RUN: llvm-objcopy -O binary --only-section=.text %t.asm.o %t.asm.bin
; RUN: llvm-objcopy -O binary --only-section=.text %t.direct.o %t.direct.bin
; RUN: cmp %t.asm.bin %t.direct.bin

; Signed IR constants must still print an encodable unsigned LDI16 operand.
define i16 @negative_constant() {
; CHECK-LABEL: negative_constant:
; CHECK: ldi16 r1, 53251
  ret i16 -12285
}
