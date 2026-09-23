; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-f32:32-f64:32-a:8:32-n8:16:32-S32"
target triple = "riscc-none-elf"

declare { i32, i1 } @llvm.umul.with.overflow.i32(i32, i32)

; Legalization splits the multiply into four native values. Reassemble its
; two i64 arguments: the first in r1:r2, the whole second argument on the stack.
define i1 @overflow(i32 %a, i32 %b) {
; CHECK-LABEL: overflow:
; CHECK:       mov [[B:r[0-6]]], r2
; CHECK:       ldi [[ZERO:r[0-6]]], 0
; CHECK:       ldi r2, 0
; CHECK-DAG:   st [[ZERO]], [r7 + 4]
; CHECK-DAG:   st [[B]], [r7 + 0]
; CHECK:       jalr s7, r0
; CHECK:       .long call_target(__muldi3)
  %result = call { i32, i1 } @llvm.umul.with.overflow.i32(i32 %a, i32 %b)
  %overflow = extractvalue { i32, i1 } %result, 1
  ret i1 %overflow
}
