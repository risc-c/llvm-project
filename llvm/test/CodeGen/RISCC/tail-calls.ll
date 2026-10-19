; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,FULL,SYS
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,NANO

target triple = "riscc-none-elf"

declare i16 @callee(i16)
declare i16 @four_args(i16, i16, i16, i16)
declare i16 @five_args(i16, i16, i16, i16, i16)

define i16 @direct_tail(i16 %x) {
; CHECK-LABEL: direct_tail:
; FULL:        jall s0, callee
; MIN:         ldi16 r0, callee
; MIN-NEXT:    jalr s0, r0
; NANO:        ldi16 r0, callee
; NANO-NEXT:   jalr r0, r0
; CHECK-NOT:   ret
  %result = tail call i16 @callee(i16 %x)
  ret i16 %result
}

; A sibling call happens after restoring our callee-saved registers. Its
; clobber mask must not force these temporary saves onto the stack.
define i16 @callee_saved_tail(i16 %x) {
; CHECK-LABEL: callee_saved_tail:
; FULL-NOT:    addi r7,
; MIN-NOT:     addi r7,
; FULL:        mts s2, r4
; MIN:         mts s2, r4
; NANO:        st r4,
; FULL:        mfs r4, s2
; MIN:         mfs r4, s2
; NANO:        ld r4,
; FULL:        jall s0, callee
; MIN:         jalr s0, r0
; NANO:        jalr r0, r0
  call void asm sideeffect "", "~{r4}"()
  %result = tail call i16 @callee(i16 %x)
  ret i16 %result
}

; The fourth argument is already in the correct incoming stack slot. No frame,
; stack copying or return-link preservation is needed to forward it unchanged.
define i16 @direct_tail_four_args(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: direct_tail_four_args:
; CHECK-NOT:   addi r7,
; CHECK-NOT:   ld r
; CHECK-NOT:   st r
; FULL:        jall s0, four_args
; MIN:         ldi16 r0, four_args
; MIN-NEXT:    jalr s0, r0
; NANO:        ldi16 r0, four_args
; NANO-NEXT:   jalr r0, r0
  %result = tail call i16 @four_args(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %result
}

define i16 @indirect_tail(ptr %callee_ptr, i16 %x) {
; CHECK-LABEL: indirect_tail:
; CHECK:       mov r3, r1
; CHECK:       mov r1, r2
; FULL:        jalr s0, r3
; MIN:         jalr s0, r3
; NANO:        jalr r0, r3
; CHECK-NOT:   ret
  %result = tail call i16 %callee_ptr(i16 %x)
  ret i16 %result
}

define i16 @framed_tail(i16 %x) {
; CHECK-LABEL: framed_tail:
; CHECK:       addi r7, -2
; CHECK-NEXT:  st r1, [r7 + 0]
; CHECK-NEXT:  ld r1, [r7 + 0]
; CHECK-NEXT:  addi r7, 2
; FULL-NEXT:   jall s0, callee
; MIN-NEXT:    ldi16 r0, callee
; MIN-NEXT:    jalr s0, r0
; NANO-NEXT:   ldi16 r0, callee
; NANO-NEXT:   jalr r0, r0
  %slot = alloca i16, align 2
  store volatile i16 %x, ptr %slot
  %value = load volatile i16, ptr %slot
  %result = tail call i16 @callee(i16 %value)
  ret i16 %result
}

define i16 @call_then_tail(i16 %x) {
; CHECK-LABEL: call_then_tail:
; FULL:        jall s7, callee
; FULL:        mts s7,
; FULL:        jall s0, callee
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

; r3 keeps the target live across the large epilogue's r0 scratch use.
define i16 @large_indirect_tail(ptr %callee_ptr, i16 %x) {
; CHECK-LABEL: large_indirect_tail:
; CHECK:       ldi r0, 200
; CHECK:       sub r7, r7,
; CHECK:       mov r3, r1
; CHECK:       ldi r0, 200
; CHECK-NEXT:  add r7, r7, r0
; FULL-NEXT:   jalr s0, r3
; MIN-NEXT:    jalr s0, r3
; NANO-NEXT:   jalr r0, r3
  %frame = alloca [100 x i16], align 2
  %slot = getelementptr [100 x i16], ptr %frame, i16 0, i16 99
  store volatile i16 %x, ptr %slot
  %value = load volatile i16, ptr %slot
  %result = tail call i16 %callee_ptr(i16 %value)
  ret i16 %result
}

define i16 @forward_stack_tail(i16 %a, i16 %b, i16 %c, i16 %d, i16 %e) {
; CHECK-LABEL: forward_stack_tail:
; CHECK-NOT:   addi r7,
; FULL:        jall s0, five_args
; MIN:         jalr s0, r0
; NANO:        jalr r0, r0
  %result = tail call i16 @five_args(i16 %a, i16 %b, i16 %c, i16 %d, i16 %e)
  ret i16 %result
}

define i16 @reject_indirect_four_args(
    ptr %callee_ptr, i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: reject_indirect_four_args:
; FULL:        jalr s7, r{{[0-6]}}
; MIN:         jalr s7, r{{[0-6]}}
; NANO:        jalr r6, r{{[0-6]}}
; SYS:         jalr s0, r0
; MIN:         jalr s0, r0
; NANO:        jalr r0, r{{[0-6]}}
  %result = tail call i16 %callee_ptr(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %result
}

define void @discard_return(i16 %x) {
; CHECK-LABEL: discard_return:
; FULL: jall s0, callee
; MIN: jalr s0, r0
; NANO: jalr r0, r0
; CHECK-NOT: ret
; CHECK: .Lfunc_end
  %result = tail call i16 @callee(i16 %x)
  ret void
}

; All three argument registers are occupied, so the indirect target cannot
; use r3 across the epilogue and must remain an ordinary call.
define i16 @reject_indirect_three_args(ptr %fn, i16 %a, i16 %b, i16 %c) {
; CHECK-LABEL: reject_indirect_three_args:
; FULL: jalr s7,
; MIN: jalr s7,
; NANO: jalr r6,
  %r = tail call i16 %fn(i16 %a, i16 %b, i16 %c)
  ret i16 %r
}
