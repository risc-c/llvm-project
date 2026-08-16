; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

declare void @llvm.va_start(ptr)

; The first unnamed argument is at the incoming stack pointer. The fixed
; parameter remains in r1 and is not spilled to create a va_list home area.
define i16 @take_one(i16 %fixed, ...) {
; CHECK-LABEL: take_one:
; CHECK:       addi r7, -4
; CHECK:       mov [[AP:r[0-6]]], r7
; CHECK-NEXT:  addi [[AP]], 4
; CHECK-NEXT:  st [[AP]], [r7 + 2]
; CHECK-NEXT:  ld r1, [r7 + 4]
; CHECK:       addi r7, 4
; CHECK:       ret s7
  %ap = alloca ptr, align 2
  call void @llvm.va_start(ptr %ap)
  %next = load ptr, ptr %ap, align 2
  %value = load i16, ptr %next, align 2
  ret i16 %value
}

; CHECK-LABEL: call_take_one:
; CHECK:       addi r7, -4
; CHECK:       st r0, [r7 + 2]
; CHECK:       ldi [[VARARG:r[0-6]]], 9
; CHECK-NEXT:  st [[VARARG]], [r7 + 0]
; CHECK:       ldi r1, 7
; CHECK:       jall s7, take_one
; CHECK:       ld r0, [r7 + 2]
; CHECK:       addi r7, 4
define i16 @call_take_one() {
  %value = call i16 (i16, ...) @take_one(i16 7, i16 9)
  ret i16 %value
}
