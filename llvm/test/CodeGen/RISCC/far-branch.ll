; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,SYS
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,SYS
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -filetype=obj < %s | llvm-objdump -dr - | FileCheck %s --check-prefix=OBJ

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

; A conditional destination beyond the signed eight-bit word displacement is
; expanded to inverse-condition-over-JALL.  The link destination is S0, so the
; far jump does not overwrite the function's return address.
define i16 @far_conditional(i16 %a, i16 %b) {
; RC32-LABEL: far_conditional:
; RC32:       bnez [[NEAR:.LBB[0-9_]+]]
; RC32:       ldpc r0, {{.Ltmp[0-9]+}}
; RC32-NEXT:  jalr s0, r0
; CHECK-LABEL: far_conditional:
; CHECK:       sub r0, r1, r2
; SYS-NEXT:    bnez [[NEAR:.LBB[0-9_]+]]
; SYS:         jmpl [[FAR:.LBB[0-9_]+]]
; MIN-NEXT:    bnez [[NEAR:.LBB[0-9_]+]]
; MIN:         li r0, [[FAR:.LBB[0-9_]+]]
; MIN:         jalr s0, r0
; CHECK:       .zero 300
; CHECK:       [[FAR]]:
; OBJ:         sub r0, r1, r2
; OBJ-NEXT:    {{.*}}bnez .+6
; OBJ-NEXT:    {{.*}}jall s0, 0
; OBJ-NEXT:    {{.*}}R_RISCC_CODE16 .text+0x{{[0-9a-f]+}}
  %same = icmp eq i16 %a, %b
  br i1 %same, label %far, label %near, !prof !0

near:
  call void asm sideeffect ".space 300", ""()
  ret i16 0

far:
  ret i16 1
}

!0 = !{!"branch_weights", i32 1, i32 1000}
