; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -verify-machineinstrs < %s | FileCheck %s --check-prefix=MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -verify-machineinstrs < %s | FileCheck %s --check-prefix=NANO
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -filetype=obj < %s | llvm-readobj -r - | FileCheck %s --check-prefix=RELOC

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

@function_pointer = global ptr @callee, align 2
@isolated_function_pointer = global ptr @only_pointer_target, align 2

declare i16 @callee(i16)
declare void @only_pointer_target()
declare i16 @six_args(i16, i16, i16, i16, i16, i16)
declare i16 @four_args(i16, i16, i16, i16)

define i16 @direct_call(i16 %x) {
; ASM-LABEL: direct_call:
; ASM:       jall s7, callee
; ASM:       ret s7
  %v = call i16 @callee(i16 %x)
  ret i16 %v
}

define i16 @indirect_call(ptr %fp, i16 %x) {
; ASM-LABEL: indirect_call:
; ASM:       jalr s7, {{r[0-6]}}
; ASM:       ret s7
  %v = call i16 %fp(i16 %x)
  ret i16 %v
}

define i16 @stack_call() {
; ASM-LABEL: stack_call:
; The maximum outgoing area is reserved once in the prologue.  S7 is saved
; above the three stack arguments, and SP is not adjusted around the call.
; ASM:       addi r7, -8
; ASM:       st r0, [r7 + 6]
; ASM:       st {{r[0-6]}}, [r{{[0-7]}} + 4]
; ASM:       st {{r[0-6]}}, [r{{[0-7]}} + 2]
; ASM:       st {{r[0-6]}}, [r{{[0-7]}} + 0]
; ASM-NOT:   addi r7
; ASM:       jall s7, six_args
; ASM-NEXT:  ld r0, [r7 + 6]
; ASM:       addi r7, 8
  %v = call i16 @six_args(i16 1, i16 2, i16 3, i16 4, i16 5, i16 6)
  ret i16 %v
}

; The fourth argument is stack-passed. r0 remains a caller-saved scratch
; register for loading the argument and materializing direct-call targets.
define i16 @four_slot_call(i16 %a, i16 %b, i16 %c, i16 %d) {
; ASM-LABEL: four_slot_call:
; ASM:       st r0, [r{{[0-7]}} + 0]
; ASM:       jall s7, four_args
; ASM:       ret s7
; MIN-LABEL: four_slot_call:
; MIN:       st r0, [r{{[0-7]}} + 0]
; MIN:       ldi16 r0, four_args
; MIN-NEXT:  jalr s7, r0
; MIN:       ret s7
; NANO-LABEL: four_slot_call:
; NANO:       st r0, [r{{[0-7]}} + 0]
; NANO:       ldi16 r0, four_args
; NANO-NEXT:  jalr r6, r0
; NANO:       jalr r0, r0
  %v = call i16 @four_args(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %v
}

; A function pointer is an ordinary native byte address.
; RELOC: Relocations [
; RELOC: Section {{.*}} .rela.data {
; RELOC-NEXT: 0x0 R_RISCC_ABS16 callee 0x0
; RELOC-NEXT: 0x2 R_RISCC_ABS16 only_pointer_target 0x0
; RELOC-NEXT: }
; RELOC-NEXT: ]
