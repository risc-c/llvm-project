; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -filetype=obj < %s | llvm-readobj -r - | FileCheck %s --check-prefix=RELOC

target datalayout = "e-m:e-P1-p:16:16-p1:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

@function_pointer = global ptr addrspace(1) @callee, align 2
@isolated_function_pointer = global ptr addrspace(1) @only_pointer_target, align 2

declare i16 @callee(i16)
declare void @only_pointer_target()
declare i16 @six_args(i16, i16, i16, i16, i16, i16)

define i16 @direct_call(i16 %x) {
; ASM-LABEL: direct_call:
; ASM:       call16 code(callee)
; ASM:       rets
  %v = call i16 @callee(i16 %x)
  ret i16 %v
}

define i16 @indirect_call(ptr addrspace(1) %fp, i16 %x) {
; ASM-LABEL: indirect_call:
; ASM:       call {{r[0-6]}}
; ASM:       rets
  %v = call i16 %fp(i16 %x)
  ret i16 %v
}

define i16 @stack_call() {
; ASM-LABEL: stack_call:
; The maximum outgoing area is reserved once in the prologue.  S7 is saved
; above the two stack arguments, and SP is not adjusted around the call.
; ASM:       addi r7, -6
; ASM:       st r0, [r7 + 4]
; ASM:       st {{r[0-6]}}, [r7 + 2]
; ASM:       st {{r[0-6]}}, [r7 + 0]
; ASM-NOT:   addi r7
; ASM:       call16 code(six_args)
; ASM-NEXT:  ld r0, [r7 + 4]
; ASM:       addi r7, 6
  %v = call i16 @six_args(i16 1, i16 2, i16 3, i16 4, i16 5, i16 6)
  ret i16 %v
}

; A function pointer is stored in its native instruction-memory word-address
; representation, not as an ordinary byte-addressed data relocation.
; RELOC: Relocations [
; RELOC: Section {{.*}} .rela.data {
; RELOC-NEXT: 0x0 R_RISCC_CODE16 callee 0x0
; RELOC-NEXT: 0x2 R_RISCC_CODE16 only_pointer_target 0x0
; RELOC-NEXT: }
; RELOC-NEXT: ]
