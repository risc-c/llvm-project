; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -O2 -verify-machineinstrs < %s | FileCheck %s

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

define i32 @mul32(i32 %left, i32 %right) {
; CHECK-LABEL: mul32:
; CHECK:       __mulsi3
; CHECK-NOT:   {{[[:space:]]mul[[:space:]]}}
  %product = mul i32 %left, %right
  ret i32 %product
}
