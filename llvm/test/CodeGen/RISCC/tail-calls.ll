; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,SYS
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,SYS
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,NANO

target triple = "riscc-none-elf"

declare i16 @callee(i16)
declare i16 @four_args(i16, i16, i16, i16)
declare i16 @five_args(i16, i16, i16, i16, i16)

define i16 @direct_tail(i16 %x) {
; CHECK-LABEL: direct_tail:
; SYS:         tail code(callee)
; MIN:         li r0, code(callee)
; MIN-NEXT:    jal s0, r0
; NANO:        li r0, code(callee)
; NANO-NEXT:   jal r0, r0
; CHECK-NOT:   ret
  %result = tail call i16 @callee(i16 %x)
  ret i16 %result
}

define i16 @indirect_tail(ptr addrspace(1) %callee_ptr, i16 %x) {
; CHECK-LABEL: indirect_tail:
; CHECK:       tail r{{[0-4]}}
; CHECK-NOT:   ret
  %result = tail call i16 %callee_ptr(i16 %x)
  ret i16 %result
}

define i16 @framed_tail(i16 %x) {
; CHECK-LABEL: framed_tail:
; CHECK:       addi r7, -2
; CHECK-NEXT:  stw r1, [r7 + 0]
; CHECK-NEXT:  ldw r1, [r7 + 0]
; CHECK-NEXT:  addi r7, 2
; SYS-NEXT:    tail code(callee)
; MIN-NEXT:    li r0, code(callee)
; MIN-NEXT:    jal s0, r0
; NANO-NEXT:   li r0, code(callee)
; NANO-NEXT:   jal r0, r0
  %slot = alloca i16, align 2
  store volatile i16 %x, ptr %slot
  %value = load volatile i16, ptr %slot
  %result = tail call i16 @callee(i16 %value)
  ret i16 %result
}

define i16 @call_then_tail(i16 %x) {
; CHECK-LABEL: call_then_tail:
; SYS:         call16 code(callee)
; SYS:         mts s7,
; SYS:         tail code(callee)
; MIN:         jal s7,
; MIN:         mts s7,
; MIN:         jal s0,
; NANO:        jal r6,
; NANO:        ldw r6,
; NANO:        jal r0,
  %first = call i16 @callee(i16 %x)
  %result = tail call i16 @callee(i16 %first)
  ret i16 %result
}

define i16 @large_indirect_tail(ptr addrspace(1) %callee_ptr, i16 %x) {
; CHECK-LABEL: large_indirect_tail:
; CHECK:       ldi r{{[0-4]}}, 202
; CHECK:       sub r7, r7,
; CHECK:       ldi r{{[0-4]}}, 202
; CHECK-NEXT:  add r7, r7,
; CHECK-NEXT:  tail r{{[0-4]}}
  %frame = alloca [100 x i16], align 2
  %slot = getelementptr [100 x i16], ptr %frame, i16 0, i16 99
  store volatile i16 %x, ptr %slot
  %value = load volatile i16, ptr %slot
  %result = tail call i16 %callee_ptr(i16 %value)
  ret i16 %result
}

define i16 @reject_stack_tail(i16 %a, i16 %b, i16 %c, i16 %d, i16 %e) {
; CHECK-LABEL: reject_stack_tail:
; SYS:         call16 code(five_args)
; SYS:         rets
; MIN:         jal s7,
; MIN:         rets
; NANO:        jal r6,
; NANO:        ret r{{[0-6]}}
  %result = tail call i16 @five_args(i16 %a, i16 %b, i16 %c, i16 %d, i16 %e)
  ret i16 %result
}

define i16 @reject_indirect_four_args(
    ptr addrspace(1) %callee_ptr, i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: reject_indirect_four_args:
; CHECK:       call r{{[0-6]}}
; SYS:         rets
; MIN:         rets
; NANO:        ret r{{[0-6]}}
  %result = tail call i16 %callee_ptr(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %result
}
