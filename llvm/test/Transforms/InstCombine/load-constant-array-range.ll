; RUN: opt -passes=instcombine -S < %s | FileCheck %s
; RUN: opt -data-layout="E-p:64:64-i32:32" -passes=instcombine -S < %s | FileCheck %s

target datalayout = "e-p:64:64-i32:32"

@table = private constant [4 x i32] [i32 4, i32 7, i32 17, i32 23]
@mutable = global [4 x i32] [i32 4, i32 7, i32 17, i32 23]
@external = external constant [4 x i32]
@replaceable = weak constant [4 x i32] [i32 4, i32 7, i32 17, i32 23]
@with_undef = private constant [4 x i32] [i32 4, i32 undef, i32 17, i32 23]
@with_poison = private constant [4 x i32] [i32 4, i32 poison, i32 17, i32 23]
@signed = private constant [4 x i32] [i32 -3, i32 -1, i32 0, i32 2]

define i32 @mask(i64 %index) {
; CHECK-LABEL: @mask(
; CHECK: load i32, ptr {{.*}}, align 4, !range ![[RANGE:[0-9]+]]
; CHECK-NOT: and i32
; CHECK: ret i32
  %p = getelementptr inbounds [4 x i32], ptr @table, i64 0, i64 %index
  %v = load i32, ptr %p, align 4
  %r = and i32 %v, 31
  ret i32 %r
}

define i32 @byte_stride(i64 %index) {
; CHECK-LABEL: @byte_stride(
; CHECK: load i32, ptr {{.*}}, align 1, !range ![[RANGE]]
; CHECK-NOT: and i32
; CHECK: ret i32
  %p = getelementptr inbounds [4 x i8], ptr @table, i64 %index
  %v = load i32, ptr %p, align 1
  %r = and i32 %v, 31
  ret i32 %r
}

define i1 @signed_range(i64 %index) {
; CHECK-LABEL: @signed_range(
; CHECK: ret i1 true
  %p = getelementptr inbounds [4 x i32], ptr @signed, i64 0, i64 %index
  %v = load i32, ptr %p
  %r = icmp slt i32 %v, 3
  ret i1 %r
}

define i32 @mutable_array(i64 %index) {
; CHECK-LABEL: @mutable_array(
; CHECK: load i32, ptr {{.*}}, align 4{{$}}
  %p = getelementptr inbounds [4 x i32], ptr @mutable, i64 0, i64 %index
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @external_array(i64 %index) {
; CHECK-LABEL: @external_array(
; CHECK: load i32, ptr {{.*}}, align 4{{$}}
  %p = getelementptr inbounds [4 x i32], ptr @external, i64 0, i64 %index
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @interposable_array(i64 %index) {
; CHECK-LABEL: @interposable_array(
; CHECK: load i32, ptr {{.*}}, align 4{{$}}
  %p = getelementptr inbounds [4 x i32], ptr @replaceable, i64 0, i64 %index
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @volatile_load(i64 %index) {
; CHECK-LABEL: @volatile_load(
; CHECK: load volatile i32, ptr {{.*}}, align 4{{$}}
  %p = getelementptr inbounds [4 x i32], ptr @table, i64 0, i64 %index
  %v = load volatile i32, ptr %p
  ret i32 %v
}

define i32 @atomic_load(i64 %index) {
; CHECK-LABEL: @atomic_load(
; CHECK: load atomic i32, ptr {{.*}} monotonic, align 4{{$}}
  %p = getelementptr inbounds [4 x i32], ptr @table, i64 0, i64 %index
  %v = load atomic i32, ptr %p monotonic, align 4
  ret i32 %v
}

define i32 @unknown_byte_offset(i64 %index) {
; CHECK-LABEL: @unknown_byte_offset(
; CHECK: load i32, ptr {{.*}}, align 1{{$}}
  %p = getelementptr inbounds i8, ptr @table, i64 %index
  %v = load i32, ptr %p, align 1
  ret i32 %v
}

define i32 @misaligned_element(i64 %index) {
; CHECK-LABEL: @misaligned_element(
; CHECK: load i32, ptr {{.*}}, align 1{{$}}
  %p = getelementptr inbounds [4 x i8], ptr @table, i64 %index, i64 1
  %v = load i32, ptr %p, align 1
  ret i32 %v
}

define i16 @different_type(i64 %index) {
; CHECK-LABEL: @different_type(
; CHECK: load i16, ptr {{.*}}, align 2{{$}}
  %p = getelementptr inbounds [4 x i32], ptr @table, i64 0, i64 %index
  %v = load i16, ptr %p, align 2
  ret i16 %v
}

define i32 @undef_element(i64 %index) {
; CHECK-LABEL: @undef_element(
; CHECK: load i32, ptr {{.*}}, align 4{{$}}
  %p = getelementptr inbounds [4 x i32], ptr @with_undef, i64 0, i64 %index
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @poison_element(i64 %index) {
; CHECK-LABEL: @poison_element(
; CHECK: load i32, ptr {{.*}}, align 4{{$}}
  %p = getelementptr inbounds [4 x i32], ptr @with_poison, i64 0, i64 %index
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @non_inbounds(i64 %index) {
; CHECK-LABEL: @non_inbounds(
; CHECK: load i32, ptr {{.*}}, align 4{{$}}
  %p = getelementptr [4 x i32], ptr @table, i64 0, i64 %index
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @existing_range(i64 %index) {
; CHECK-LABEL: @existing_range(
; CHECK: load i32, ptr {{.*}}, align 4, !range ![[EXISTING:[0-9]+]]
  %p = getelementptr inbounds [4 x i32], ptr @table, i64 0, i64 %index
  %v = load i32, ptr %p, !range !0
  ret i32 %v
}

; CHECK: ![[RANGE]] = !{i32 4, i32 24}
; CHECK: ![[EXISTING]] = !{i32 4, i32 8}
!0 = !{i32 4, i32 8}
