; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -enable-new-pm -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC32

; Turn equality into an all-ones mask without first inverting the comparison.
; RC16-LABEL: mask16:
; RC16: sltu
; RC16-NEXT: addi {{r[0-6]}}, -1
; RC16-NEXT: ret s7
define i16 @mask16(i16 %a, i16 %b) {
  %c = icmp eq i16 %a, %b
  %z = zext i1 %c to i16
  %r = sub i16 0, %z
  ret i16 %r
}

; RC32-LABEL: mask32:
; RC32: sltu
; RC32-NEXT: addi {{r[0-6]}}, -1
; RC32-NEXT: ret s7
define i32 @mask32(i32 %a, i32 %b) {
  %c = icmp eq i32 %a, %b
  %z = zext i1 %c to i32
  %r = sub i32 0, %z
  ret i32 %r
}

; Inequality already uses the cheaper comparison and must not be inverted.
; RC32-LABEL: not_equal:
; RC32: sltu
; RC32-NEXT: sub
; RC32-NEXT: ret s7
define i32 @not_equal(i32 %a, i32 %b) {
  %c = icmp ne i32 %a, %b
  %z = zext i1 %c to i32
  %r = sub i32 0, %z
  ret i32 %r
}
