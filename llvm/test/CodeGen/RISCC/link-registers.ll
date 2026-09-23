; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,MIN

target triple = "riscc-none-elf"

declare void @external()

define internal i16 @private_leaf(i16 %value) {
; CHECK-LABEL: private_leaf:
; CHECK:       ret s3
  %result = add i16 %value, 1
  ret i16 %result
}

define i16 @call_private(i16 %value) {
; CHECK-LABEL: call_private:
; CHECK-NOT:   addi r7,
; CHECK-NOT:   mfs
; FULL:        jall s3, private_leaf
; MIN:         ldi16 r0, private_leaf
; MIN-NEXT:    jalr s3, r0
; CHECK-NEXT:  ret s7
  %result = call i16 @private_leaf(i16 %value)
  ret i16 %result
}

define i16 @musttail_private(i16 %value) {
; CHECK-LABEL: musttail_private:
; CHECK:       mfs r0, s7
; CHECK-NEXT:  mts s3, r0
; FULL-NEXT:   jall s0, private_leaf
; MIN-NEXT:    ldi16 r0, private_leaf
; MIN-NEXT:    jalr s0, r0
; CHECK-NOT:   ret
  %result = musttail call i16 @private_leaf(i16 %value)
  ret i16 %result
}

define internal void @private_nonleaf() {
; CHECK-LABEL: private_nonleaf:
; CHECK:       mfs r0, s3
; CHECK:       st r0,
; FULL:        jall s7, external
; MIN:         jalr s7,
; CHECK:       ld r0,
; CHECK-NEXT:  addi r7, 2
; CHECK-NEXT:  mts s3, r0
; CHECK:       ret s3
  call void @external()
  ret void
}

define void @call_private_nonleaf() {
; CHECK-LABEL: call_private_nonleaf:
; FULL:        jall s3, private_nonleaf
; MIN:         ldi16 r0, private_nonleaf
; MIN-NEXT:    jalr s3, r0
  call void @private_nonleaf()
  ret void
}

define internal i16 @private_tail_target(i16 %value) {
; CHECK-LABEL: private_tail_target:
; CHECK:       ret s3
  %result = add i16 %value, 2
  ret i16 %result
}

define internal i16 @private_tail_caller(i16 %value) {
; CHECK-LABEL: private_tail_caller:
; FULL:        jall s0, private_tail_target
; MIN:         ldi16 r0, private_tail_target
; MIN-NEXT:    jalr s0, r0
; CHECK-NOT:   ret
  %result = tail call i16 @private_tail_target(i16 %value)
  ret i16 %result
}

; Keep the private tail-call chain reachable when IPRA traverses callees first.
define i16 @call_private_tail(i16 %value) {
; CHECK-LABEL: call_private_tail:
; FULL:        jall s3, private_tail_caller
; MIN:         ldi16 r0, private_tail_caller
; MIN-NEXT:    jalr s3, r0
  %result = call i16 @private_tail_caller(i16 %value)
  ret i16 %result
}

; A clobbered incoming link can live in a spare S register. Return directly
; through the saved address; the original link is caller-saved.
define void @clobber_public_link() {
; CHECK-LABEL: clobber_public_link:
; CHECK:       mfs r0, s7
; CHECK-NEXT:  mts s2, r0
; CHECK-NOT:   st
; CHECK-NOT:   ld
; CHECK:       ret s2
  call void asm sideeffect "", "~{s7}"()
  ret void
}

; A tail call using the other link convention must forward the entry address,
; not the link left by the preceding call. Cover both directions, with the
; address saved either in a spare S register or on the stack.
define i16 @public_leaf(i16 %value) noinline {
  %result = add i16 %value, 3
  ret i16 %result
}

define i16 @public_call_private_tail(i16 %value) {
; CHECK-LABEL: public_call_private_tail:
; CHECK:       mfs r0, s7
; CHECK-NEXT:  mts s2, r0
; FULL:        jall s7, public_leaf
; MIN:         jalr s7, r0
; CHECK-NEXT:  mfs r0, s2
; CHECK-NEXT:  mts s3, r0
; FULL-NEXT:   jall s0, private_leaf
; MIN-NEXT:    ldi16 r0, private_leaf
; MIN-NEXT:    jalr s0, r0
  %called = call i16 @public_leaf(i16 %value)
  %result = tail call i16 @private_leaf(i16 %called)
  ret i16 %result
}

define i16 @public_stack_private_tail() {
; CHECK-LABEL: public_stack_private_tail:
; CHECK:       mfs r0, s7
; CHECK-NEXT:  st r0, [r7 + 0]
; FULL:        jall s7, external
; MIN:         jalr s7, r0
; CHECK:       ld r0, [r7 + 0]
; CHECK:       mts s3, r0
; CHECK-NEXT:  addi r7, 2
; FULL-NEXT:   jall s0, private_leaf
; MIN-NEXT:    ldi16 r0, private_leaf
; MIN-NEXT:    jalr s0, r0
  call void @external()
  %result = tail call i16 @private_leaf(i16 4)
  ret i16 %result
}

define internal i16 @private_call_public_tail(i16 %value) {
; CHECK-LABEL: private_call_public_tail:
; CHECK:       mfs r0, s3
; CHECK-NEXT:  mts s2, r0
; FULL:        jall s3, private_leaf
; MIN:         jalr s3, r0
; CHECK-NEXT:  mfs r0, s2
; CHECK-NEXT:  mts s7, r0
; FULL-NEXT:   jall s0, public_leaf
; MIN-NEXT:    ldi16 r0, public_leaf
; MIN-NEXT:    jalr s0, r0
  %called = call i16 @private_leaf(i16 %value)
  %result = tail call i16 @public_leaf(i16 %called)
  ret i16 %result
}

define internal i16 @private_stack_public_tail() {
; CHECK-LABEL: private_stack_public_tail:
; CHECK:       mfs r0, s3
; CHECK-NEXT:  st r0, [r7 + 0]
; FULL:        jall s7, external
; MIN:         jalr s7, r0
; CHECK:       ld r0, [r7 + 0]
; CHECK:       mts s7, r0
; CHECK-NEXT:  addi r7, 2
; FULL-NEXT:   jall s0, public_leaf
; MIN-NEXT:    ldi16 r0, public_leaf
; MIN-NEXT:    jalr s0, r0
  call void @external()
  %result = tail call i16 @public_leaf(i16 4)
  ret i16 %result
}

; Keep the internal callers reachable to IPRA.
define i16 @use_private_callers(i16 %value) {
  %a = call i16 @private_call_public_tail(i16 %value)
  %b = call i16 @private_stack_public_tail()
  %sum = add i16 %a, %b
  ret i16 %sum
}

; An outgoing call defines the link, but a non-returning caller never needs
; its entry value. Do not allocate a register or stack slot to preserve it.
define void @call_forever() noreturn {
; CHECK-LABEL: call_forever:
; CHECK-NOT:   addi r7,
; CHECK-NOT:   mfs
; CHECK-NOT:   mts
; CHECK-NOT:   st
; FULL:        jall s7, external
; MIN:         jalr s7, r0
entry:
  br label %loop
loop:
  call void @external()
  br label %loop
}
