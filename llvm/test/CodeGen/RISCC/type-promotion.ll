; REQUIRES: riscc-registered-target
; RUN: llc -mcpu=full -verify-machineinstrs -print-after=type-promotion %s -o /dev/null 2>&1 | FileCheck %s \
; RUN:   --check-prefix=RC16
; RUN: sed 's/p:16:16/p:32:32/' %s | llc -mcpu=full -mattr=+rc32 -verify-machineinstrs -print-after=type-promotion \
; RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=RC32
; RUN: llc -enable-new-pm -mcpu=full -verify-machineinstrs -print-after=typepromotion %s -o /dev/null 2>&1 | \
; RUN:   FileCheck %s --check-prefix=RC16
; RUN: sed 's/p:16:16/p:32:32/' %s | llc -enable-new-pm -mcpu=full -mattr=+rc32 -verify-machineinstrs \
; RUN:   -print-after=typepromotion -o /dev/null 2>&1 | FileCheck %s --check-prefix=RC32
; RUN: llc -enable-new-pm -O0 -mcpu=full -verify-machineinstrs -debug-pass-manager %s -o /dev/null 2>&1 | \
; RUN:   FileCheck %s --check-prefix=O0

; O0: Running pass: AtomicExpandPass
; O0-NOT: TypePromotionPass
; O0: Running pass: RISCCAsmPrinterEndPass

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-n8:16-S16"
target triple = "riscc-none-elf"

; Promote a non-wrapping narrow counter before LSR changes it to a countdown.
; RC16 must not use the generic TTI default of 32 bits for native promotion.
define void @counted(ptr %out) optsize {
; RC16-LABEL: define void @counted
; RC16:       phi i16
; RC16:       add nuw nsw i16
; RC16:       icmp eq i16
; RC32-LABEL: define void @counted
; RC32:       phi i32
; RC32:       add nuw nsw i32
; RC32:       icmp eq i32
entry:
  br label %loop
loop:
  %counter = phi i8 [ 0, %entry ], [ %next, %loop ]
  store volatile i8 %counter, ptr %out
  %next = add nuw nsw i8 %counter, 1
  %done = icmp eq i8 %next, 8
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
