; REQUIRES: riscc-registered-target
; RUN: opt -mtriple=riscc -mcpu=full -mattr=+rc32 -passes='default<O2>' -verify-each -S %s | FileCheck %s
; RUN: opt -mtriple=riscc -mcpu=full -mattr=+rc32 -passes='default<O3>' -verify-each -S %s | FileCheck %s
; RUN: sed 's/p:32:32/p:16:16/' %s | opt -mtriple=riscc -mcpu=full -passes='default<O2>' -verify-each -S - | \
; RUN:   FileCheck %s
; RUN: sed 's/) {/) optsize {/' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 -passes='default<O2>' -verify-each -S \
; RUN:   - | FileCheck %s

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"

; Pointer rotation alone must not force unrolling. Use the ordinary LLVM
; profitability policy, just as for other unknown-trip-count loops.
define ptr @reverse(ptr %head) {
; CHECK-LABEL: define {{.*}}ptr @reverse(
; CHECK: load ptr
; CHECK: store ptr
; CHECK-NOT: load ptr
; CHECK: ret ptr
entry:
  %empty = icmp eq ptr %head, null
  br i1 %empty, label %exit, label %loop
loop:
  %current = phi ptr [ %head, %entry ], [ %next, %loop ]
  %previous = phi ptr [ null, %entry ], [ %current, %loop ]
  %next = load ptr, ptr %current
  store ptr %previous, ptr %current
  %done = icmp eq ptr %next, null
  br i1 %done, label %exit, label %loop
exit:
  %result = phi ptr [ null, %entry ], [ %current, %loop ]
  ret ptr %result
}

; Explicit source unroll-disable is respected.
define ptr @disabled(ptr %head) {
; CHECK-LABEL: define {{.*}}ptr @disabled(
; CHECK: load ptr
; CHECK: store ptr
; CHECK-NOT: load ptr
; CHECK: ret ptr
entry:
  %empty = icmp eq ptr %head, null
  br i1 %empty, label %exit, label %loop
loop:
  %current = phi ptr [ %head, %entry ], [ %next, %loop ]
  %previous = phi ptr [ null, %entry ], [ %current, %loop ]
  %next = load ptr, ptr %current
  store ptr %previous, ptr %current
  %done = icmp eq ptr %next, null
  br i1 %done, label %exit, label %loop, !llvm.loop !0
exit:
  %result = phi ptr [ null, %entry ], [ %current, %loop ]
  ret ptr %result
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.unroll.disable"}

; Volatile accesses do not opt into any special unrolling policy either.
define ptr @volatile_reverse(ptr %head) {
; CHECK-LABEL: define {{.*}}ptr @volatile_reverse(
; CHECK: load volatile ptr
; CHECK-NOT: load volatile ptr
; CHECK: ret ptr
entry:
  %empty = icmp eq ptr %head, null
  br i1 %empty, label %exit, label %loop
loop:
  %current = phi ptr [ %head, %entry ], [ %next, %loop ]
  %previous = phi ptr [ null, %entry ], [ %current, %loop ]
  %next = load volatile ptr, ptr %current
  store ptr %previous, ptr %current
  %done = icmp eq ptr %next, null
  br i1 %done, label %exit, label %loop
exit:
  %result = phi ptr [ null, %entry ], [ %current, %loop ]
  ret ptr %result
}

; Additional loop-carried state also uses the ordinary profitability policy.
define ptr @three_pointers(ptr %head) {
; CHECK-LABEL: define {{.*}}ptr @three_pointers(
; CHECK: load ptr
; CHECK-NOT: load ptr
; CHECK: ret ptr
entry:
  %empty = icmp eq ptr %head, null
  br i1 %empty, label %exit, label %loop
loop:
  %current = phi ptr [ %head, %entry ], [ %next, %loop ]
  %previous = phi ptr [ null, %entry ], [ %current, %loop ]
  %older = phi ptr [ null, %entry ], [ %previous, %loop ]
  %next = load ptr, ptr %current
  store ptr %older, ptr %current
  %done = icmp eq ptr %next, null
  br i1 %done, label %exit, label %loop
exit:
  %result = phi ptr [ null, %entry ], [ %current, %loop ]
  ret ptr %result
}

; Explicit source unrolling remains available. Each expanded iteration must
; retain its null check, including for odd-length and empty lists.
define ptr @requested(ptr %head) {
; CHECK-LABEL: define {{.*}}ptr @requested(
; CHECK: load ptr
; CHECK: store ptr
; CHECK: icmp eq ptr {{.*}}, null
; CHECK: br i1
; CHECK: load ptr
; CHECK: store ptr
; CHECK: icmp eq ptr {{.*}}, null
; CHECK-NOT: load ptr
; CHECK: ret ptr
entry:
  %empty = icmp eq ptr %head, null
  br i1 %empty, label %exit, label %loop
loop:
  %current = phi ptr [ %head, %entry ], [ %next, %loop ]
  %previous = phi ptr [ null, %entry ], [ %current, %loop ]
  %next = load ptr, ptr %current
  store ptr %previous, ptr %current
  %done = icmp eq ptr %next, null
  br i1 %done, label %exit, label %loop, !llvm.loop !2
exit:
  %result = phi ptr [ null, %entry ], [ %current, %loop ]
  ret ptr %result
}

!2 = distinct !{!2, !3}
!3 = !{!"llvm.loop.unroll.count", i32 2}
