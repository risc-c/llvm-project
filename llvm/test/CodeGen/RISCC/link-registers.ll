; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,SYS
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
; SYS:         jal16 s3, code(private_leaf)
; MIN:         li r0, code(private_leaf)
; MIN-NEXT:    jalr s3, r0
  %result = call i16 @private_leaf(i16 %value)
  ret i16 %result
}

define i16 @musttail_private(i16 %value) {
; CHECK-LABEL: musttail_private:
; CHECK:       mfs r0, s7
; CHECK-NEXT:  mts s3, r0
; SYS-NEXT:    jal16 s0, code(private_leaf)
; MIN-NEXT:    li r0, code(private_leaf)
; MIN-NEXT:    jalr s0, r0
; CHECK-NOT:   ret
  %result = musttail call i16 @private_leaf(i16 %value)
  ret i16 %result
}

define internal void @private_nonleaf() {
; CHECK-LABEL: private_nonleaf:
; CHECK:       mfs r0, s3
; CHECK:       stw r0,
; SYS:         call16 code(external)
; MIN:         jalr s7,
; CHECK:       ldw r0,
; CHECK-NEXT:  mts s3, r0
; CHECK:       ret s3
  call void @external()
  ret void
}

define void @call_private_nonleaf() {
; CHECK-LABEL: call_private_nonleaf:
; SYS:         jal16 s3, code(private_nonleaf)
; MIN:         li r0, code(private_nonleaf)
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
; SYS:         jal16 s0, code(private_tail_target)
; MIN:         li r0, code(private_tail_target)
; MIN-NEXT:    jalr s0, r0
; CHECK-NOT:   ret
  %result = tail call i16 @private_tail_target(i16 %value)
  ret i16 %result
}

; An explicit clobber of the incoming link forces it to the data stack. The
; clobbered S register is not available to the software cache.
define void @clobber_public_link() {
; CHECK-LABEL: clobber_public_link:
; CHECK:       mfs r0, s7
; CHECK-NEXT:  stw r0,
; CHECK:       ldw r0,
; CHECK-NEXT:  mts s7, r0
; CHECK:       rets
  call void asm sideeffect "", "~{s7}"()
  ret void
}
