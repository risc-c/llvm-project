; REQUIRES: riscc-registered-target
; RUN: llc -verify-machineinstrs -mtriple=riscc -mcpu=full -mattr=+rc32,+mulhu < %s | FileCheck %s --check-prefix=RC32
; RUN: llc -verify-machineinstrs -mtriple=riscc -mcpu=full -mattr=+mulhu < %s | FileCheck %s --check-prefix=RC16

; Native div/rem by any nonzero constant should share the expanded quotient,
; including size builds. A variable divisor retains the combined helper.
define i32 @unsigned10(i32 %x, ptr %remainder) optsize {
; RC32-LABEL: unsigned10:
; RC32-NOT: __udiv
; RC32-NOT: __riscc_udiv
; RC32: mulhu
; RC32-NOT: __udiv
; RC32-NOT: __riscc_udiv
; RC32: ret
  %q = udiv i32 %x, 10
  %r = urem i32 %x, 10
  store i32 %r, ptr %remainder
  ret i32 %q
}

define i32 @signed26(i32 %x, ptr %remainder) optsize {
; RC32-LABEL: signed26:
; RC32-NOT: __div
; RC32-NOT: __riscc_div
; RC32: mulhu
; RC32-NOT: __div
; RC32-NOT: __riscc_div
; RC32: ret
  %q = sdiv i32 %x, 26
  %r = srem i32 %x, 26
  store i32 %r, ptr %remainder
  ret i32 %q
}

define i16 @unsigned7(i16 %x, ptr %remainder) optsize {
; RC16-LABEL: unsigned7:
; RC16-NOT: __udiv
; RC16: mulhu
; RC16-NOT: __udiv
; RC16: ret
  %q = udiv i16 %x, 7
  %r = urem i16 %x, 7
  store i16 %r, ptr %remainder
  ret i16 %q
}

define i16 @signed_minus7(i16 %x, ptr %remainder) optsize {
; RC16-LABEL: signed_minus7:
; RC16-NOT: __div
; RC16: mulhu
; RC16-NOT: __div
; RC16: ret
  %q = sdiv i16 %x, -7
  %r = srem i16 %x, -7
  store i16 %r, ptr %remainder
  ret i16 %q
}

define i32 @variable32(i32 %x, i32 %divisor, ptr %remainder) optsize {
; RC32-LABEL: variable32:
; RC32: __riscc_udivmodsi
  %q = udiv i32 %x, %divisor
  %r = urem i32 %x, %divisor
  store i32 %r, ptr %remainder
  ret i32 %q
}
