; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc -mcpu=min -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -riscc-loop-range-masks=false %s -o - | FileCheck %s \
; RUN:   --check-prefix=OFF

; The induction variable is 0..2, even though ordinary per-block DAG known
; bits cannot see the backedge bound. Removing its mask must not require any
; width-specific implementation or a different shift-helper calling convention.
define void @bounded16(i16 %v, ptr %out) optsize {
; RC16-LABEL: bounded16:
; RC16-NOT:   andi {{r[0-6]}}, 15
; RC16:       .size bounded16,
entry:
  br label %loop
loop:
  %i = phi i16 [ 0, %entry ], [ %next, %loop ]
  %shift = lshr i16 %v, %i
  store volatile i16 %shift, ptr %out
  %next = add nuw nsw i16 %i, 1
  %done = icmp eq i16 %next, 3
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @bounded32(i32 %v, ptr %out) optsize {
; RC32-LABEL: bounded32:
; RC32-NOT:   andi {{r[0-6]}}, 31
; RC32:       .size bounded32,
; OFF-LABEL:  bounded32:
; OFF:        andi {{r[0-6]}}, 31
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %shift = lshr i32 %v, %i
  store volatile i32 %shift, ptr %out
  %next = add nuw nsw i32 %i, 1
  %done = icmp eq i32 %next, 3
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; A bounded induction variable still needs a mask that intentionally discards
; significant bits. This case is not a redundant helper safety mask.
define void @significant_mask32(ptr %out) optsize {
; RC32-LABEL: significant_mask32:
; RC32:       andi {{r[0-6]}}, 1
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %masked = and i32 %i, 1
  store volatile i32 %masked, ptr %out
  %next = add nuw nsw i32 %i, 1
  %done = icmp eq i32 %next, 3
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; Counts beginning at an unknown value or crossing unsigned wrap must retain
; the mask, including for the computed-entry speed helper.
define void @unknown32(i32 %v, i32 %start, ptr %out) {
; RC32-LABEL: unknown32:
; RC32:       andi {{r[0-6]}}, 31
entry:
  br label %loop
loop:
  %i = phi i32 [ %start, %entry ], [ %next, %loop ]
  %shift = ashr i32 %v, %i
  store volatile i32 %shift, ptr %out
  %next = add i32 %i, 1
  %done = icmp eq i32 %next, 3
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @wrapping16(i16 %v, ptr %out) {
; RC16-LABEL: wrapping16:
; RC16:       andi {{r[0-6]}}, 15
entry:
  br label %loop
loop:
  %i = phi i16 [ -2, %entry ], [ %next, %loop ]
  %shift = shl i16 %v, %i
  store volatile i16 %shift, ptr %out
  %next = add i16 %i, 1
  %done = icmp eq i16 %next, 2
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
