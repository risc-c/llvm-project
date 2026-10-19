; REQUIRES: riscc-registered-target
; RUN: opt -verify-each -passes=riscc-prepare -S %s | FileCheck %s --check-prefixes=IR,RC32
; RUN: sed 's/p:32:32/p:16:16/' %s | opt -verify-each -passes=riscc-prepare -S - | FileCheck %s --check-prefixes=IR,RC16
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o /dev/null

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"
target triple = "riscc-none-elf"

; Only the low byte of the recurrence is observable, even after wrapping.
define i8 @byte_sum(ptr %p, i32 %n) {
; IR-LABEL: define i8 @byte_sum
; IR:       %acc.wide = phi i{{16|32}} [ -6, %entry ], [ %next.wide, %loop ]
; IR:       %next.wide = add i{{16|32}} %acc.wide, {{.*}}
; IR:       exit:
; IR-NEXT:  %[[RET:[^ ]+]] = trunc i{{16|32}} %next.wide to i8
; IR-NEXT:  ret i8 %[[RET]]
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %acc = phi i8 [ 250, %entry ], [ %next, %loop ]
  %addr = getelementptr i8, ptr %p, i32 %i
  %x = load i8, ptr %addr
  %next = add i8 %acc, %x
  %inc = add i32 %i, 1
  %done = icmp eq i32 %inc, %n
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; Nested PHIs, selections and an exit PHI are promoted together. Negative
; increments remain small signed constants instead of becoming wide masks.
define signext i16 @nested(i32 %n, i1 %condition) {
; IR-LABEL: define signext i16 @nested
; RC32:     %outeracc.wide = phi i32
; RC32:     %acc.wide = phi i32
; RC32:     %step.wide = select i1 %condition, i32 10, i32 -1
; RC32:     %next.wide = add i32
; RC32:     %result.wide = phi i32
; RC32:     trunc i32 %result.wide to i16
; RC16:     %outeracc = phi i16
; RC16:     %acc = phi i16
; RC16:     %next = add i16
entry:
  %empty = icmp eq i32 %n, 0
  br i1 %empty, label %exit, label %outer
outer:
  %i = phi i32 [ 0, %entry ], [ %incouter, %latch ]
  %outeracc = phi i16 [ 32760, %entry ], [ %next, %latch ]
  br label %inner
inner:
  %j = phi i32 [ 0, %outer ], [ %incinner, %inner ]
  %acc = phi i16 [ %outeracc, %outer ], [ %next, %inner ]
  %step = select i1 %condition, i16 10, i16 -1
  %next = add i16 %acc, %step
  %incinner = add i32 %j, 1
  %innerdone = icmp eq i32 %incinner, %n
  br i1 %innerdone, label %latch, label %inner
latch:
  %incouter = add i32 %i, 1
  %outerdone = icmp eq i32 %incouter, %n
  br i1 %outerdone, label %exit, label %outer
exit:
  %result = phi i16 [ 0, %entry ], [ %next, %latch ]
  ret i16 %result
}

; A comparison in the loop observes wrapping, so leave the recurrence alone.
define i8 @observed(i8 %start) {
; IR-LABEL: define i8 @observed
; IR:       %acc = phi i8
; IR:       %next = add i8 %acc, 1
entry:
  br label %loop
loop:
  %acc = phi i8 [ %start, %entry ], [ %next, %loop ]
  %next = add i8 %acc, 1
  %done = icmp eq i8 %next, 0
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; Right shifts can move upper bits into the observable result.
define i8 @shifted(i8 %start, i1 %done) {
; IR-LABEL: define i8 @shifted
; IR:       %acc = phi i8
; IR:       %sum = add i8 %acc, 10
; IR:       %next = lshr i8 %sum, 1
entry:
  br label %loop
loop:
  %acc = phi i8 [ %start, %entry ], [ %next, %loop ]
  %sum = add i8 %acc, 10
  %next = lshr i8 %sum, 1
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; The original narrow overflow flags must not be copied to widened updates.
define i8 @flagged(i8 %start, i1 %done) {
; IR-LABEL: define i8 @flagged
; IR:       %acc.wide = phi i{{16|32}}
; IR:       %next.wide = add i{{16|32}}
entry:
  br label %loop
loop:
  %acc = phi i8 [ %start, %entry ], [ %next, %loop ]
  %next = add nuw nsw i8 %acc, 1
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; Wrapping polynomial recurrences need normalization only at the exit.
define i8 @byte_polynomial(i8 %start, i8 %input, i1 %done) {
; IR-LABEL: define i8 @byte_polynomial
; IR: %acc.wide = phi i{{16|32}}
; IR: %product.wide = mul i{{16|32}} %acc.wide, 7
; IR: %shift.wide = shl i{{16|32}} %product.wide, 2
; IR: %next.wide = xor i{{16|32}} %shift.wide,
; IR: exit:
; IR-NEXT: %[[RESULT:.*]] = trunc i{{16|32}} %next.wide to i8
; IR-NEXT: ret i8 %[[RESULT]]
entry:
  br label %loop
loop:
  %acc = phi i8 [ %start, %entry ], [ %next, %loop ]
  %product = mul i8 %acc, 7
  %shift = shl i8 %product, 2
  %next = xor i8 %shift, %input
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; The count can wrap to a legal narrow shift even if the widened recurrence
; would exceed the native width. Its upper bits cannot be left unspecified.
define i8 @variable_shift(i8 %start, i1 %done) {
; IR-LABEL: define i8 @variable_shift
; IR: %count = phi i8
; IR: %next = add i8 %count, 1
; IR: %result = shl i8 %start, %next
entry:
  br label %loop
loop:
  %count = phi i8 [ -1, %entry ], [ %next, %loop ]
  %next = add i8 %count, 1
  %result = shl i8 %start, %next
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %result
}

; A multiply followed by a right shift observes the wrapped product, not
; merely its low bits. No range claim is available for the multiply here.
define i8 @multiply_then_right_shift(i8 %start, i1 %done) {
; IR-LABEL: define i8 @multiply_then_right_shift
; IR: %acc = phi i8
; IR: %product = mul i8 %acc, 3
; IR: %next = lshr i8 %product, 1
entry:
  br label %loop
loop:
  %acc = phi i8 [ %start, %entry ], [ %next, %loop ]
  %product = mul i8 %acc, 3
  %next = lshr i8 %product, 1
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}
