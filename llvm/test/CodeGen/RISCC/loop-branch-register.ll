; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -riscc-loop-branch-register -verify-machineinstrs -filetype=obj < \
; RUN:   %s -o %t.o

; End-to-end object emission for a generic loop whose carried sum is stored
; each iteration.  In particular, this exercises post-RA loop-branch
; register rewriting through the normal llc pipeline, including object
; encoding of the resulting native move instructions.
target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:32-n8:16-S16"
target triple = "riscc-none-elf"

define i16 @loop_sum_store(ptr noalias %dst, ptr noalias %src, i16 %n) {
entry:
  br label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ %next, %loop ]
  %sum = phi i16 [ 0, %entry ], [ %sum.next, %loop ]
  %v = load i16, ptr %src, align 2
  %sum.next = add i16 %sum, %v
  store i16 %sum.next, ptr %dst, align 2
  %next = add i16 %i, 1
  %done = icmp eq i16 %next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i16 %sum.next
}
