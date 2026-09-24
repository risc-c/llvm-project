; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=full -O2 -verify-machineinstrs -o - | FileCheck %s

; Equal register pressure: carry the stored value as the induction variable,
; avoiding a copy and subtraction on every iteration.
; CHECK-LABEL: fill:
; CHECK: [[LOOP:.LBB[0-9_]+]]:
; CHECK-NOT: mov
; CHECK: st [[VALUE:r[0-7]]],
; CHECK: addi [[VALUE]], 1
; CHECK-NOT: -64
; CHECK: bnez [[LOOP]]
define void @fill(ptr %p) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %v = add i32 %i, -64
  %q = getelementptr i32, ptr %p, i32 %i
  store volatile i32 %v, ptr %q, align 4
  %next = add nuw i32 %i, 1
  %done = icmp eq i32 %next, 256
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
