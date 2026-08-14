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
; SYS:         tail callee
; MIN:         li r0, callee
; MIN-NEXT:    jalr s0, r0
; NANO:        li r0, callee
; NANO-NEXT:   jalr r0, r0
; CHECK-NOT:   ret
  %result = tail call i16 @callee(i16 %x)
  ret i16 %result
}

; The fourth argument is stack-passed, so this is a normal call rather than a
; sibling call. Direct targets consistently use the caller-saved r0 scratch.
define i16 @direct_tail_four_args(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: direct_tail_four_args:
; SYS:         call16 four_args
; MIN:         li r0, four_args
; MIN-NEXT:    jalr s7, r0
; NANO:        li r0, four_args
; NANO-NEXT:   jalr r6, r0
; NANO:        ret r0
  %result = tail call i16 @four_args(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %result
}

define i16 @reject_indirect_tail(ptr %callee_ptr, i16 %x) {
; CHECK-LABEL: reject_indirect_tail:
; CHECK:       call r{{[0-6]}}
; SYS:         rets
; MIN:         rets
; NANO:        ret r{{[0-6]}}
  %result = tail call i16 %callee_ptr(i16 %x)
  ret i16 %result
}

define i16 @framed_tail(i16 %x) {
; CHECK-LABEL: framed_tail:
; CHECK:       addi r7, -2
; CHECK-NEXT:  st r1, [r7 + 0]
; CHECK-NEXT:  ld r1, [r7 + 0]
; CHECK-NEXT:  addi r7, 2
; SYS-NEXT:    tail callee
; MIN-NEXT:    li r0, callee
; MIN-NEXT:    jalr s0, r0
; NANO-NEXT:   li r0, callee
; NANO-NEXT:   jalr r0, r0
  %slot = alloca i16, align 2
  store volatile i16 %x, ptr %slot
  %value = load volatile i16, ptr %slot
  %result = tail call i16 @callee(i16 %value)
  ret i16 %result
}

define i16 @call_then_tail(i16 %x) {
; CHECK-LABEL: call_then_tail:
; SYS:         call16 callee
; SYS:         mts s7,
; SYS:         tail callee
; MIN:         jalr s7,
; MIN:         mts s7,
; MIN:         jalr s0,
; NANO:        jalr r6,
; NANO:        ld r6,
; NANO:        jalr r0,
  %first = call i16 @callee(i16 %x)
  %result = tail call i16 @callee(i16 %first)
  ret i16 %result
}

define i16 @reject_large_indirect_tail(ptr %callee_ptr, i16 %x) {
; CHECK-LABEL: reject_large_indirect_tail:
; CHECK:       ldi r{{[0-6]}}, 20{{[46]}}
; CHECK:       sub r7, r7,
; CHECK:       call r{{[0-6]}}
; SYS:         ldi r{{[0-6]}}, 20{{[46]}}
; SYS:         add r7, r7,
; MIN:         ldi r{{[0-6]}}, 20{{[46]}}
; MIN:         add r7, r7,
; NANO:        ldi r6, 204
; NANO-NEXT:   add r7, r7, r6
; SYS-NEXT:    rets
; MIN-NEXT:    rets
; NANO-NEXT:   ret r{{[0-6]}}
  %frame = alloca [100 x i16], align 2
  %slot = getelementptr [100 x i16], ptr %frame, i16 0, i16 99
  store volatile i16 %x, ptr %slot
  %value = load volatile i16, ptr %slot
  %result = tail call i16 %callee_ptr(i16 %value)
  ret i16 %result
}

define i16 @reject_stack_tail(i16 %a, i16 %b, i16 %c, i16 %d, i16 %e) {
; CHECK-LABEL: reject_stack_tail:
; SYS:         call16 five_args
; SYS:         rets
; MIN:         jalr s7,
; MIN:         rets
; NANO:        jalr r6,
; NANO:        ret r{{[0-6]}}
  %result = tail call i16 @five_args(i16 %a, i16 %b, i16 %c, i16 %d, i16 %e)
  ret i16 %result
}

define i16 @reject_indirect_four_args(
    ptr %callee_ptr, i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: reject_indirect_four_args:
; CHECK:       call r{{[0-6]}}
; SYS:         rets
; MIN:         rets
; NANO:        ret r{{[0-6]}}
  %result = tail call i16 %callee_ptr(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %result
}
