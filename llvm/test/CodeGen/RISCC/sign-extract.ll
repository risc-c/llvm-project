; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32,+mulhu < %s | FileCheck %s --check-prefixes=RC32,ALL
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+mulhu < %s | FileCheck %s --check-prefix=RC16

; Size-sensitive internal sign corrections around MULHU should not expand to
; four arithmetic shifts. Native top-bit extraction needs only a zero compare.
define i32 @signbit32(i32 %x) optsize {
; RC32-LABEL: signbit32:
; RC32: ldi [[ZERO:r[0-7]]], 0
; RC32-NEXT: slt r1, r1, [[ZERO]]
; RC32-NEXT: ret
  %r = lshr i32 %x, 31
  ret i32 %r
}

define i64 @signed_product(i32 %a, i32 %b) optsize {
; RC32-LABEL: signed_product:
; RC32-NOT: srai
; RC32: mulhu
; RC32-NOT: srai
; RC32: ret
  %aa = sext i32 %a to i64
  %bb = sext i32 %b to i64
  %r = mul i64 %aa, %bb
  ret i64 %r
}

define i16 @signbit16(i16 %x) {
; RC16-LABEL: signbit16:
; RC16: ldi [[ZERO:r[0-7]]], 0
; RC16-NEXT: slt r1, r1, [[ZERO]]
; RC16-NEXT: ret
  %r = lshr i16 %x, 15
  ret i16 %r
}

define i16 @sizemask16(i16 %x) optsize {
; RC16-LABEL: sizemask16:
; RC16: srai r0, r1, 8
; RC16-NEXT: srai r1, r0, 7
; RC16-NEXT: ret
  %r = ashr i16 %x, 15
  ret i16 %r
}

; Keep the original OR inputs available for funnel-shift matching.
define i32 @rotate_one(i32 %x) optsize {
; ALL-LABEL: rotate_one:
; ALL: fsl1
  %l = shl i32 %x, 1
  %r = lshr i32 %x, 31
  %result = or i32 %l, %r
  ret i32 %result
}
