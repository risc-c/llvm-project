; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=SOFT,ALL
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32,+mdu -verify-machineinstrs < %s | FileCheck %s --check-prefixes=MDU,ALL

target triple = "riscc-none-elf"
target datalayout = "e-m:e-p:32:32-i64:32-n32-S32"

define i32 @unsigned_div(i32 %value) {
; ALL-LABEL: unsigned_div:
; SOFT:      mul
; MDU:       mulhu
; ALL-NOT:   jalr
; ALL:       ret s7
  %result = udiv i32 %value, 26
  ret i32 %result
}

define i32 @unsigned_rem(i32 %value) {
; ALL-LABEL: unsigned_rem:
; SOFT:      mul
; MDU:       mulhu
; ALL-NOT:   jalr
; ALL:       ret s7
  %result = urem i32 %value, 26
  ret i32 %result
}

define i32 @signed_div(i32 %value) {
; ALL-LABEL: signed_div:
; SOFT:      srai {{r[0-7]}}, r1, 8
; SOFT:      srai {{r[0-7]}}, {{r[0-7]}}, 8
; SOFT:      mul
; MDU:       mulhu
; ALL-NOT:   jalr
; ALL:       ret s7
  %result = sdiv i32 %value, 26
  ret i32 %result
}

; A negative magic multiplier also uses signed half-word products. Its
; sign must be handled in the partial products, without unsigned correction.
define i32 @signed_div_negative(i32 %value) {
; ALL-LABEL: signed_div_negative:
; SOFT:      srai {{r[0-7]}}, r1, 8
; SOFT:      srai {{r[0-7]}}, {{r[0-7]}}, 8
; SOFT:      mul
; MDU:       mulhu
; ALL-NOT:   jalr
; ALL:       ret s7
  %result = sdiv i32 %value, -26
  ret i32 %result
}

define i32 @signed_rem(i32 %value) {
; ALL-LABEL: signed_rem:
; SOFT:      mul
; MDU:       mulhu
; ALL-NOT:   jalr
; ALL:       ret s7
  %result = srem i32 %value, 26
  ret i32 %result
}

define i32 @unsigned_div_size(i32 %value) optsize {
; ALL-LABEL: unsigned_div_size:
; SOFT:      __udivsi3
; MDU:       mulhu
; MDU:       ret s7
  %result = udiv i32 %value, 26
  ret i32 %result
}

define i32 @signed_rem_size(i32 %value) optsize {
; ALL-LABEL: signed_rem_size:
; SOFT:      __modsi3
; MDU:       mulhu
; MDU:       ret s7
  %result = srem i32 %value, 26
  ret i32 %result
}


; Power-of-two operations remain cheap even under the size policy.
define i32 @div_pow2_size(i32 %value) optsize {
; ALL-LABEL: div_pow2_size:
; ALL-NOT:   jalr
; ALL-NOT:   call_target
; ALL:       ret s7
  %result = sdiv i32 %value, 2
  ret i32 %result
}

; Power-of-two operations remain cheap even under the size policy.
define i32 @rem_pow2_size(i32 %value) optsize {
; ALL-LABEL: rem_pow2_size:
; ALL-NOT:   jalr
; ALL-NOT:   call_target
; ALL:       ret s7
  %result = srem i32 %value, 4
  ret i32 %result
}

; Power-of-two operations remain cheap even under the size policy.
define i32 @div_negative_pow2_size(i32 %value) optsize {
; ALL-LABEL: div_negative_pow2_size:
; ALL-NOT:   jalr
; ALL-NOT:   call_target
; ALL:       ret s7
  %result = sdiv i32 %value, -8
  ret i32 %result
}
