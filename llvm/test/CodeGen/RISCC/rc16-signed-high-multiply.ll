; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefixes=SOFT,ALL
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+mdu -verify-machineinstrs < %s | FileCheck %s --check-prefixes=MDU,ALL

target triple = "riscc-none-elf"
target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:32-f32:16-f64:16-a:8:16-n8:16-S16"

; Signed magic division needs the signed high product. Sign-extending the
; upper limbs lets the four native multiplies produce it directly.
define i16 @signed_div(i16 %value) {
; ALL-LABEL: signed_div:
; SOFT:      srai {{r[0-7]}}, r1, 8
; SOFT:      mul
; MDU:       mulhu
; ALL-NOT:   jall
; ALL:       ret s7
  %result = sdiv i16 %value, 26
  ret i16 %result
}

define i16 @signed_div_negative(i16 %value) {
; ALL-LABEL: signed_div_negative:
; SOFT:      srai {{r[0-7]}}, r1, 8
; SOFT:      mul
; MDU:       mulhu
; ALL-NOT:   jall
; ALL:       ret s7
  %result = sdiv i16 %value, -26
  ret i16 %result
}

; Unsigned high multiplication keeps logical upper limbs and carries.
define i16 @unsigned_div(i16 %value) {
; ALL-LABEL: unsigned_div:
; SOFT:      srli {{r[0-7]}}, r1, 8
; SOFT:      mul
; MDU:       mulhu
; ALL-NOT:   jall
; ALL:       ret s7
  %result = udiv i16 %value, 26
  ret i16 %result
}
