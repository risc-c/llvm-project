; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

@word = global i16 0, align 2

define i16 @add16(i16 %a, i16 %b) {
; CHECK-LABEL: add16:
; CHECK:       add r1, r1, r2
; CHECK-NEXT:  rets
  %v = add i16 %a, %b
  ret i16 %v
}

define i16 @logic16(i16 %a, i16 %b) {
; CHECK-LABEL: logic16:
; CHECK:       or r1, r1, r2
; CHECK-NEXT:  rets
  %x = xor i16 %a, %b
  %y = and i16 %a, %b
  %v = or i16 %x, %y
  ret i16 %v
}

define i16 @mul16(i16 %a, i16 %b) {
; CHECK-LABEL: mul16:
; CHECK:       mul r1, r1, r2
; CHECK-NEXT:  rets
  %v = mul i16 %a, %b
  ret i16 %v
}

define i16 @shl16(i16 %a) {
; CHECK-LABEL: shl16:
; CHECK:       slli r1, r1, 7
; CHECK-NEXT:  rets
  %v = shl i16 %a, 7
  ret i16 %v
}

define i16 @lshr16(i16 %a) {
; CHECK-LABEL: lshr16:
; CHECK:       srli r1, r1, 3
; CHECK-NEXT:  rets
  %v = lshr i16 %a, 3
  ret i16 %v
}

define i16 @ashr16(i16 %a) {
; CHECK-LABEL: ashr16:
; CHECK:       srai r1, r1, 8
; CHECK-NEXT:  rets
  %v = ashr i16 %a, 8
  ret i16 %v
}

define i16 @constant16() {
; CHECK-LABEL: constant16:
; CHECK:       li r1, 4660
; CHECK-NEXT:  rets
  ret i16 4660
}

define i16 @load16(ptr %p) {
; CHECK-LABEL: load16:
; CHECK:       ld r1, [r1 + 0]
; CHECK-NEXT:  rets
  %v = load i16, ptr %p, align 2
  ret i16 %v
}

define void @store16(ptr %p, i16 %v) {
; CHECK-LABEL: store16:
; CHECK:       st r2, [r1 + 0]
; CHECK-NEXT:  rets
  store i16 %v, ptr %p, align 2
  ret void
}

define i16 @load_global() {
; CHECK-LABEL: load_global:
; CHECK:       li [[ADDR:r[0-6]]], word
; CHECK-NEXT:  ld r1, {{\[}}[[ADDR]] + 0]
; CHECK-NEXT:  rets
  %v = load i16, ptr @word, align 2
  ret i16 %v
}
