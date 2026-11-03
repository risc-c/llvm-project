; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC32

; Reassociate only when the unshifted mask is a cheap immediate and the
; shifted value is not shared. Bits masked below the shift are already zero.
define i32 @byte_mask(i32 %x) {
; RC32-LABEL: byte_mask:
; RC32-NEXT:  ; %bb.0:
; RC32-NEXT:  andi r1, 255
; RC32-NEXT:  slli r1, r1, 8
; RC32-NEXT:  ret s7
  %shift = shl i32 %x, 8
  %masked = and i32 %shift, 65283
  ret i32 %masked
}

define i32 @byte_mask_size(i32 %x) optsize {
; RC32-LABEL: byte_mask_size:
; RC32:       andi r1, 255
; RC32-NEXT:  slli r1, r1, 3
; RC32-NEXT:  ret s7
  %shift = shl i32 %x, 3
  %masked = and i32 %shift, 2040
  ret i32 %masked
}

; The low word of a legalized i64 can use the same combine.
define i64 @wide_byte_mask(i64 %x) {
; RC32-LABEL: wide_byte_mask:
; RC32:       andi r1, 255
; RC32-NEXT:  slli r1, r1, 8
; RC32-NEXT:  ldi r2, 0
; RC32-NEXT:  ret s7
  %shift = shl i64 %x, 8
  %masked = and i64 %shift, 65280
  ret i64 %masked
}

; A halfword mask does not fit ANDI, so leave it in its original position.
define i32 @halfword_mask(i32 %x) {
; RC32-LABEL: halfword_mask:
; RC32:       slli {{r[0-6]}}, {{r[0-6]}}, 8
; RC32:       and {{r[0-6]}}, {{r[0-6]}}, {{r[0-6]}}
  %shift = shl i32 %x, 8
  %masked = and i32 %shift, 16776960
  ret i32 %masked
}

define i32 @shared_shift(i32 %x, ptr %p) {
; RC32-LABEL: shared_shift:
; RC32:       slli [[SHIFT:r[0-6]]], r1, 8
; RC32:       and r1, [[SHIFT]], {{r[0-6]}}
; RC32:       st [[SHIFT]], [r2 + 0]
  %shift = shl i32 %x, 8
  store i32 %shift, ptr %p, align 4
  %masked = and i32 %shift, 65280
  ret i32 %masked
}
