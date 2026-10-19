; REQUIRES: riscc-registered-target
; RUN: opt -verify-each -passes=riscc-prepare -S %s | FileCheck %s
; RUN: sed 's/p:32:32/p:16:16/g; s/i32/i16/g' %s | opt -verify-each -passes=riscc-prepare -S - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o /dev/null

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"
target triple = "riscc-none-elf"

; Rebase a byte scan, including uses of both the current and updated pointers
; on exits. The original inbounds promises must not apply to the new pointers.
define ptr @scan(ptr %start, ptr %out) {
; CHECK-LABEL: define ptr @scan(
; CHECK: entry:
; CHECK: %address.initial = getelementptr i8, ptr %start, i{{16|32}} -3
; CHECK: loop:
; CHECK-NEXT: %address.rebased = phi ptr [ %address.initial, %entry ], [ %address.next, %latch ]
; CHECK-NEXT: %[[LOADADDR:[^ ]+]] = getelementptr i8, ptr %address.rebased, i{{16|32}} 0
; CHECK-NEXT: %c = load i8, ptr %[[LOADADDR]]
; CHECK: latch:
; CHECK-NEXT: %address.next = getelementptr i8, ptr %address.rebased, i{{16|32}} 1
; CHECK: exit:
; CHECK: %address.adjusted{{[0-9]*}} = getelementptr i8, ptr %address.rebased, i{{16|32}} 1
; CHECK: exit.next:
; CHECK: getelementptr i8, ptr %address.next, i{{16|32}} -1
entry:
  br label %loop
loop:
  %p = phi ptr [ %start, %entry ], [ %next, %latch ]
  %address = getelementptr inbounds i8, ptr %p, i32 -3
  %c = load i8, ptr %address
  %zero = icmp eq i8 %c, 0
  br i1 %zero, label %exit, label %latch
latch:
  %next = getelementptr inbounds i8, ptr %p, i32 1
  %comma = icmp eq i8 %c, 44
  br i1 %comma, label %exit.next, label %loop
exit:
  %after = getelementptr i8, ptr %p, i32 -2
  store ptr %after, ptr %out
  ret ptr %p
exit.next:
  %previous = getelementptr i8, ptr %next, i32 -4
  ret ptr %previous
}

; Do not introduce recovery on an exit PHI's incoming edge inside the loop.
define ptr @exit_phi(ptr %start, i1 %skip) {
; CHECK-LABEL: define ptr @exit_phi(
; CHECK: loop:
; CHECK-NEXT: %p = phi ptr
; CHECK-NOT: address.rebased
; CHECK: exit:
; CHECK-NEXT: %result = phi ptr [ %start, %entry ], [ %p, %loop ]
entry:
  br i1 %skip, label %exit, label %preheader
preheader:
  br label %loop
loop:
  %p = phi ptr [ %start, %preheader ], [ %next, %loop ]
  %address = getelementptr i8, ptr %p, i32 2
  %c = load i8, ptr %address
  %next = getelementptr i8, ptr %p, i32 1
  %done = icmp eq i8 %c, 0
  br i1 %done, label %exit, label %loop
exit:
  %result = phi ptr [ %start, %entry ], [ %p, %loop ]
  ret ptr %result
}

; Two distinct addresses inside the loop could require a second live pointer.
define void @two_offsets(ptr %start, i1 %done) {
; CHECK-LABEL: define void @two_offsets(
; CHECK-NOT: address.rebased
; CHECK: %p = phi ptr
; CHECK-NOT: address.rebased
; CHECK: ret void
entry:
  br label %loop
loop:
  %p = phi ptr [ %start, %entry ], [ %next, %loop ]
  %a = getelementptr i8, ptr %p, i32 -1
  %b = getelementptr i8, ptr %p, i32 1
  store i8 1, ptr %a
  store i8 2, ptr %b
  %next = getelementptr i8, ptr %p, i32 2
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; Native-word accesses already have an immediate displacement.
define i32 @word_offset(ptr %start, i1 %done) {
; CHECK-LABEL: define i{{16|32}} @word_offset(
; CHECK-NOT: address.rebased
; CHECK: %p = phi ptr
; CHECK-NOT: address.rebased
; CHECK: ret i{{16|32}} %v
entry:
  br label %loop
loop:
  %p = phi ptr [ %start, %entry ], [ %next, %loop ]
  %a = getelementptr i32, ptr %p, i32 -1
  %v = load i32, ptr %a
  %next = getelementptr i32, ptr %p, i32 1
  br i1 %done, label %exit, label %loop
exit:
  ret i32 %v
}

; A comparison of the original pointer inside the loop needs it unadjusted.
define void @pointer_test(ptr %start, ptr %end) {
; CHECK-LABEL: define void @pointer_test(
; CHECK-NOT: address.rebased
; CHECK: %p = phi ptr
; CHECK-NOT: address.rebased
; CHECK: ret void
entry:
  br label %loop
loop:
  %p = phi ptr [ %start, %entry ], [ %next, %loop ]
  %a = getelementptr i8, ptr %p, i32 -1
  store i8 1, ptr %a
  %next = getelementptr i8, ptr %p, i32 1
  %done = icmp eq ptr %p, %end
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
