; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -O2 -verify-machineinstrs < %s | FileCheck %s

target datalayout = "e-m:e-P1-p:16:16-p1:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

define i32 @unsigned_div(i32 %left, i32 %right) {
; CHECK-LABEL: unsigned_div:
; CHECK:       code(__udivsi3)
; CHECK-NOT:   __udivmodsi4
  %quotient = udiv i32 %left, %right
  ret i32 %quotient
}

define i32 @unsigned_rem(i32 %left, i32 %right) {
; CHECK-LABEL: unsigned_rem:
; CHECK:       code(__umodsi3)
; CHECK-NOT:   __udivmodsi4
  %remainder = urem i32 %left, %right
  ret i32 %remainder
}

define i32 @unsigned_pair(i32 %left, i32 %right, ptr %out) {
; CHECK-LABEL: unsigned_pair:
; CHECK:       code(__udivmodsi4)
; CHECK-NOT:   __udivsi3
; CHECK-NOT:   __mulsi3
  %quotient = udiv i32 %left, %right
  %product = mul i32 %quotient, %right
  %remainder = sub i32 %left, %product
  store i32 %remainder, ptr %out, align 2
  ret i32 %quotient
}

define i32 @signed_div(i32 %left, i32 %right) {
; CHECK-LABEL: signed_div:
; CHECK:       code(__divsi3)
; CHECK-NOT:   __divmodsi4
  %quotient = sdiv i32 %left, %right
  ret i32 %quotient
}

define i32 @signed_rem(i32 %left, i32 %right) {
; CHECK-LABEL: signed_rem:
; CHECK:       code(__modsi3)
; CHECK-NOT:   __divmodsi4
  %remainder = srem i32 %left, %right
  ret i32 %remainder
}

define i32 @signed_pair(i32 %left, i32 %right, ptr %out) {
; CHECK-LABEL: signed_pair:
; CHECK:       code(__divmodsi4)
; CHECK-NOT:   __divsi3
; CHECK-NOT:   __mulsi3
  %quotient = sdiv i32 %left, %right
  %product = mul i32 %quotient, %right
  %remainder = sub i32 %left, %product
  store i32 %remainder, ptr %out, align 2
  ret i32 %quotient
}
