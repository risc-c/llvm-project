; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefix=LONG
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=LONG,SYS
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s -o /dev/null
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -filetype=obj < %s -o %t
; RUN: llvm-readobj -r %t | FileCheck %s --check-prefix=MIN-RELOC
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -mattr=+rc32 -filetype=obj < %s -o %t.sys
; RUN: llvm-readobj -r %t.sys | FileCheck %s --check-prefix=SYS-RELOC

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"
target triple = "riscc-none-elf"

declare i32 @callee(i32)

define i32 @direct_tail(i32 %value) {
; LONG-LABEL: direct_tail:
; LONG-DAG:   ldpc r0, [[DIRECT:.Ltmp[0-9]+]]
; SYS-DAG:    .reloc {{.*}}, R_RISCC_RELAX_TAIL, [[DIRECT]]
; LONG-NEXT:  jalr s0, r0
; LONG-NOT:   ret
; LONG:       [[DIRECT]]:
; LONG-NEXT:  .long call_target(callee)
  %result = tail call i32 @callee(i32 %value)
  ret i32 %result
}

define i32 @framed_tail(i32 %value) {
; LONG-LABEL: framed_tail:
; LONG:       addi r7, -4
; LONG:       ld r1, [r7 + 0]
; LONG-NEXT:  addi r7, 4
; LONG-DAG:   ldpc r0, [[FRAMED:.Ltmp[0-9]+]]
; SYS-DAG:    .reloc {{.*}}, R_RISCC_RELAX_TAIL, [[FRAMED]]
; LONG-NEXT:  jalr s0, r0
; LONG:       [[FRAMED]]:
; LONG-NEXT:  .long call_target(callee)
  %slot = alloca i32, align 4
  store volatile i32 %value, ptr %slot
  %reloaded = load volatile i32, ptr %slot
  %result = tail call i32 @callee(i32 %reloaded)
  ret i32 %result
}

define i32 @call_then_tail(i32 %value) {
; LONG-LABEL: call_then_tail:
; LONG-DAG:   ldpc r0, [[CALLED:.Ltmp[0-9]+]]
; SYS-DAG:    .reloc {{.*}}, R_RISCC_RELAX_CALL, [[CALLED]]
; LONG-NEXT:  jalr s7, r0
; LONG:       ld r0, [r7 + 0]
; LONG-NEXT:  mts s7, r0
; LONG-NEXT:  addi r7, 4
; LONG-DAG:   ldpc r0, [[TAILED:.Ltmp[0-9]+]]
; SYS-DAG:    .reloc {{.*}}, R_RISCC_RELAX_TAIL, [[TAILED]]
; LONG-NEXT:  jalr s0, r0
; LONG:       [[CALLED]]:
; LONG-NEXT:  .long call_target(callee)
; LONG:       [[TAILED]]:
; LONG-NEXT:  .long call_target(callee)
  %first = call i32 @callee(i32 %value)
  %result = tail call i32 @callee(i32 %first)
  ret i32 %result
}

define i32 @callee_saved_tail(i32 %value) {
; LONG-LABEL: callee_saved_tail:
; LONG:       st r4, [r7 + 0]
; LONG:       ld r4, [r7 + 0]
; LONG-NEXT:  addi r7, 4
; LONG-DAG:   ldpc r0, [[SAVED:.Ltmp[0-9]+]]
; SYS-DAG:    .reloc {{.*}}, R_RISCC_RELAX_TAIL, [[SAVED]]
; LONG-NEXT:  jalr s0, r0
; LONG:       [[SAVED]]:
; LONG-NEXT:  .long call_target(callee)
  call void asm sideeffect "", "~{r4}"()
  %result = tail call i32 @callee(i32 %value)
  ret i32 %result
}

define i32 @local_call(i32 %value) {
; LONG-LABEL: local_call:
; LONG-DAG:   ldpc r0, [[LOCAL:.Ltmp[0-9]+]]
; SYS-DAG:    .reloc {{.*}}, R_RISCC_RELAX_CALL, [[LOCAL]]
; LONG:       [[LOCAL]]:
; LONG-NEXT:  .long call_target(local_callee)
  %result = call i32 @local_callee(i32 %value)
  %adjusted = add i32 %result, 1
  ret i32 %adjusted
}

define internal i32 @local_callee(i32 %value) #0 {
; LONG-LABEL: local_callee:
; LONG:       ret s7
; LONG-NOT:   ret s3
  %result = add i32 %value, 2
  ret i32 %result
}

attributes #0 = { noinline }

; MIN-RELOC-COUNT-5: R_RISCC_CALL_TARGET callee
; MIN-RELOC: R_RISCC_CALL_TARGET local_callee
; MIN-RELOC-NOT: R_RISCC_RELAX
; SYS-RELOC-DAG: R_RISCC_CALL_TARGET callee
; SYS-RELOC-DAG: R_RISCC_CALL_TARGET callee
; SYS-RELOC-DAG: R_RISCC_CALL_TARGET callee
; SYS-RELOC-DAG: R_RISCC_CALL_TARGET callee
; SYS-RELOC-DAG: R_RISCC_CALL_TARGET callee
; SYS-RELOC-DAG: R_RISCC_CALL_TARGET local_callee
; SYS-RELOC-DAG: R_RISCC_RELAX_CALL
; SYS-RELOC-DAG: R_RISCC_RELAX_CALL
; SYS-RELOC-DAG: R_RISCC_RELAX_TAIL
; SYS-RELOC-DAG: R_RISCC_RELAX_TAIL
; SYS-RELOC-DAG: R_RISCC_RELAX_TAIL
; SYS-RELOC-DAG: R_RISCC_RELAX_TAIL
