; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -filetype=obj %s -o %t
; RUN: llvm-readobj -r %t | FileCheck %s --check-prefix=RELOC
; RUN: llc -enable-new-pm -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs -filetype=obj %s -o %t.new-pm

@value = external global i32

; The load is too far from both natural pool gaps. The inline pool must be
; skipped during execution, and its load must refer to the local word.
define i32 @long_block() {
; CHECK-LABEL: long_block:
; CHECK:       .zero 300
; CHECK:       ldpc [[ADDR:r[0-7]]], [[LITERAL:.Ltmp[0-9]+]]
; CHECK-NEXT:  ld r1, {{\[}}[[ADDR]] + 0]
; CHECK-NEXT:  jmp8 [[CONTINUE:.LBB[0-9_]+]]
; CHECK:       .p2align 2
; CHECK:       [[LITERAL]]:
; CHECK-NEXT:  .long value
; CHECK-NEXT:  [[CONTINUE]]:
; CHECK:       .zero 300
  call void asm sideeffect ".space 300", ""()
  %v = load volatile i32, ptr @value
  call void asm sideeffect ".space 300", ""()
  ret i32 %v
}

; RELOC: R_RISCC_ABS32 value

declare void @callee()

define void @long_call_block() {
; CHECK-LABEL: long_call_block:
; CHECK:       .zero 300
; CHECK:       ldpc r0, [[LITERAL:.Ltmp[0-9]+]]
; CHECK-NEXT:  jalr s7, r0
; CHECK-NEXT:  jmp8 [[CONTINUE:.LBB[0-9_]+]]
; CHECK:       .p2align 2
; CHECK:       [[LITERAL]]:
; CHECK-NEXT:  .long {{(call_target\()?}}callee{{\)?}}
; CHECK-NEXT:  [[CONTINUE]]:
; CHECK:       .zero 300
  call void asm sideeffect ".space 300", ""()
  call void @callee()
  call void asm sideeffect ".space 300", ""()
  ret void
}

; RELOC: R_RISCC_RELAX_CALL
; RELOC: R_RISCC_CALL_TARGET callee

define void @long_tail_block() {
; CHECK-LABEL: long_tail_block:
; CHECK:       .zero 300
; CHECK:       .long value
; CHECK:       .zero 300
; CHECK-NOT:   jmp8
; CHECK:       ldpc r0, [[LITERAL:.Ltmp[0-9]+]]
; CHECK-NEXT:  jalr s0, r0
; CHECK:       [[LITERAL]]:
; CHECK-NEXT:  .long {{(call_target\()?}}callee{{\)?}}
  call void asm sideeffect ".space 300", ""()
  %v = load volatile i32, ptr @value
  call void asm sideeffect ".space 300", ""()
  tail call void @callee()
  ret void
}

; RELOC: R_RISCC_ABS32 value
; RELOC: R_RISCC_RELAX_TAIL
; RELOC: R_RISCC_CALL_TARGET callee

; Function alignment may put the entry farther than LDPC can reach from a
; pool before the function. Place the call's word after that alignment.
define void @aligned_function() align 4096 {
; CHECK-LABEL: aligned_function:
; CHECK:       ldpc r0, [[LITERAL:.Ltmp[0-9]+]]
; CHECK-NEXT:  jalr s7, r0
; CHECK-NEXT:  jmp8 [[CONTINUE:.LBB[0-9_]+]]
; CHECK:       [[LITERAL]]:
; CHECK-NEXT:  .long {{(call_target\()?}}callee{{\)?}}
; CHECK-NEXT:  [[CONTINUE]]:
  call void @callee()
  call void asm sideeffect ".space 300", ""()
  ret void
}
