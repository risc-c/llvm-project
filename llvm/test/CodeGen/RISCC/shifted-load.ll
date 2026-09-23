; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC16

; Move masks before shifts to enable byte/halfword loads without a literal.
; RC32-LABEL: shift_byte:
; RC32: ldb [[V:r[0-6]]], [r1]
; RC32-NEXT: slli r1, [[V]], 3
; RC32-NEXT: ret s7
define i32 @shift_byte(ptr %p) {
 %v = load i32, ptr %p, align 4
 %s = shl i32 %v, 3
 %a = and i32 %s, 2040
 ret i32 %a
}
; RC32-LABEL: shift_half:
; RC32: ldh [[V:r[0-6]]], [r1]
; RC32-NEXT: slli r1, [[V]], 3
; RC32-NEXT: ret s7
define i32 @shift_half(ptr %p) {
 %v = load i32, ptr %p, align 4
 %s = shl i32 %v, 3
 %a = and i32 %s, 524280
 ret i32 %a
}
; RC32-LABEL: shift_volatile:
; RC32: ld {{r[0-6]}}, [r1 + 0]
; RC32: and
; RC32: ret s7
define i32 @shift_volatile(ptr %p) {
 %v = load volatile i32, ptr %p, align 4
 %s = shl i32 %v, 3
 %a = and i32 %s, 2040
 ret i32 %a
}
; RC16-LABEL: shift_byte16:
; RC16: ldb [[V:r[0-6]]], [r1]
; RC16-NEXT: slli r1, [[V]], 3
; RC16-NEXT: ret s7
; RC32-LABEL: shift_byte16:
; RC32: ldb [[V:r[0-6]]], [r1]
; RC32-NEXT: slli r1, [[V]], 3
; RC32-NEXT: ret s7
define i16 @shift_byte16(ptr %p) {
 %v = load i16, ptr %p, align 2
 %s = shl i16 %v, 3
 %a = and i16 %s, 2040
 ret i16 %a
}

; Preserve the full value when another use needs the upper bits.
; RC32-LABEL: shared_load:
; RC32: ld [[V:r[0-6]]], [r1 + 0]
; RC32: st [[V]], [r2 + 0]
define i32 @shared_load(ptr %p, ptr %q) {
  %v = load i32, ptr %p, align 4
  store i32 %v, ptr %q, align 4
  %s = shl i32 %v, 3
  %a = and i32 %s, 2040
  ret i32 %a
}
