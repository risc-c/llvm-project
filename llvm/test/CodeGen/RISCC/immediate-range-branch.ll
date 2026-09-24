; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=full -verify-machineinstrs -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=nano -verify-machineinstrs -o /dev/null

; Sign-extended bytes have enough headroom for subtraction by an immediate.
; Full-width operands must retain SLT if that subtraction could overflow.

define void @byte_lt(ptr %p, ptr %out) {
; CHECK-LABEL: byte_lt:
; CHECK-NOT: slt
; CHECK: cmpi r{{[0-7]}}, 65
; CHECK-NEXT: b{{lt|ge}}z
  %byte = load volatile i8, ptr %p
  %x = sext i8 %byte to i32
  %c = icmp slt i32 %x, 65
  br i1 %c, label %yes, label %exit
yes:
  store volatile i32 71, ptr %out
  br label %exit
exit:
  ret void
}

define void @byte_ge(ptr %p, ptr %out) {
; CHECK-LABEL: byte_ge:
; CHECK-NOT: slt
; CHECK: cmpi r{{[0-7]}}, -3
; CHECK-NEXT: b{{lt|ge}}z
  %byte = load volatile i8, ptr %p
  %x = sext i8 %byte to i32
  %c = icmp sge i32 %x, -3
  br i1 %c, label %yes, label %exit
yes:
  store volatile i32 71, ptr %out
  br label %exit
exit:
  ret void
}

define void @byte_le(ptr %p, ptr %out) {
; CHECK-LABEL: byte_le:
; CHECK-NOT: slt
; CHECK: cmpi r{{[0-7]}}, 11
; CHECK-NEXT: b{{lt|ge}}z
  %byte = load volatile i8, ptr %p
  %x = sext i8 %byte to i32
  %c = icmp sle i32 %x, 10
  br i1 %c, label %yes, label %exit
yes:
  store volatile i32 71, ptr %out
  br label %exit
exit:
  ret void
}

define void @byte_gt(ptr %p, ptr %out) {
; CHECK-LABEL: byte_gt:
; CHECK-NOT: slt
; CHECK: cmpi r{{[0-7]}}, 127
; CHECK-NEXT: b{{lt|ge}}z
  %byte = load volatile i8, ptr %p
  %x = sext i8 %byte to i32
  %c = icmp sgt i32 %x, 126
  br i1 %c, label %yes, label %exit
yes:
  store volatile i32 71, ptr %out
  br label %exit
exit:
  ret void
}

define void @nonnegative(ptr %p, ptr %out) {
; CHECK-LABEL: nonnegative:
; CHECK-NOT: slt
; CHECK: cmpi r{{[0-7]}}, 65
; CHECK-NEXT: b{{lt|ge}}z
  %byte = load volatile i8, ptr %p
  %x = zext i8 %byte to i32
  %c = icmp slt i32 %x, 65
  br i1 %c, label %yes, label %exit
yes:
  store volatile i32 71, ptr %out
  br label %exit
exit:
  ret void
}

define void @native_low(ptr %p, ptr %out) {
; CHECK-LABEL: native_low:
; CHECK: slt
  %x = load volatile i32, ptr %p
  %c = icmp slt i32 %x, 65
  br i1 %c, label %yes, label %exit
yes:
  store volatile i32 71, ptr %out
  br label %exit
exit:
  ret void
}

define void @native_high(ptr %p, ptr %out) {
; CHECK-LABEL: native_high:
; CHECK: slt
  %x = load volatile i32, ptr %p
  %c = icmp sge i32 %x, -3
  br i1 %c, label %yes, label %exit
yes:
  store volatile i32 71, ptr %out
  br label %exit
exit:
  ret void
}

define void @bound_too_large(ptr %p, ptr %out) {
; CHECK-LABEL: bound_too_large:
; CHECK: slt
  %byte = load volatile i8, ptr %p
  %x = sext i8 %byte to i32
  %c = icmp sle i32 %x, 127
  br i1 %c, label %yes, label %exit
yes:
  store volatile i32 71, ptr %out
  br label %exit
exit:
  ret void
}
