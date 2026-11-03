; REQUIRES: riscc-registered-target
; RUN: llc -verify-machineinstrs -mtriple=riscc -mcpu=full -mattr=+rc32,+mulhu < %s | FileCheck %s --check-prefix=RC32
; RUN: llc -verify-machineinstrs -mtriple=riscc -mcpu=full -mattr=+mulhu < %s | FileCheck %s --check-prefix=RC16

; The low output can reuse the multiplier input. This also lets the paired
; result go straight into the return registers without an extra move.
define i64 @product32(i32 %a, i32 %b) {
; RC32-LABEL: product32:
; RC32-NEXT: {{.*}}bb.0
; RC32-NEXT: mulhu r1, r2, r1
; RC32-NEXT: ret
  %aa = zext i32 %a to i64
  %bb = zext i32 %b to i64
  %r = mul i64 %aa, %bb
  ret i64 %r
}

define i32 @product16(i16 %a, i16 %b) {
; RC16-LABEL: product16:
; RC16-NEXT: {{.*}}bb.0
; RC16-NEXT: mulhu r1, r2, r1
; RC16-NEXT: ret
  %aa = zext i16 %a to i32
  %bb = zext i16 %b to i32
  %r = mul i32 %aa, %bb
  ret i32 %r
}

; A dead low result still needs a register distinct from the live high result.
define i32 @high32(i32 %a, i32 %b) {
; RC32-LABEL: high32:
; RC32: mulhu [[LOW:r[0-7]]], r1, r2
; RC32-NEXT: ret
  %aa = zext i32 %a to i64
  %bb = zext i32 %b to i64
  %r = mul i64 %aa, %bb
  %hi = lshr i64 %r, 32
  %result = trunc i64 %hi to i32
  ret i32 %result
}
