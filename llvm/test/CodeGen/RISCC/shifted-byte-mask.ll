; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC32
; RUN: sed 's/i32/i16/g; s/p:32:32/p:16:16/; s/i16:32/i16:16/' %s | llc -mtriple=riscc -mcpu=full \
; RUN:   -verify-machineinstrs -o - | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=MIN

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"

; Both bytes of the same value are consumed, as in byte-oriented table indices.
define i32 @adjacent_bytes(i32 %x) {
; RC32-LABEL: adjacent_bytes:
; RC32-NOT: ldpc
; RC32: srli {{.*}}, 8
; RC32: andi {{.*}}, 255
; RC32-NOT: ldpc
; RC32: ret
; RC16-LABEL: adjacent_bytes:
; RC16-NOT: lui
; RC16: srli {{.*}}, 8
; RC16-NOT: lui
; RC16: ret
  %low = and i32 %x, 255
  %masked = and i32 %x, 65280
  %high = lshr i32 %masked, 8
  %sum = xor i32 %low, %high
  ret i32 %sum
}

define i32 @sparse_byte(i32 %x) {
; RC32-LABEL: sparse_byte:
; RC32-NOT: ldpc
; RC32: srli {{.*}}, 8
; RC32: andi {{.*}}, 85
; RC32-NOT: ldpc
; RC32: ret
; RC16-LABEL: sparse_byte:
; RC16-NOT: lui
; RC16: srli {{.*}}, 8
; RC16: andi {{.*}}, 85
; RC16: ret
  %masked = and i32 %x, 21760
  %high = lshr i32 %masked, 8
  ret i32 %high
}

; Keep the unshifted value intact when it has another user.
define i32 @shared_mask(i32 %x) {
; RC32-LABEL: shared_mask:
; RC32: ldpc
; RC32: and
; RC32: srli
; RC32: or
; RC16-LABEL: shared_mask:
; RC16: lui
; RC16: and
; RC16: srli
; RC16: or
  %masked = and i32 %x, 65280
  %high = lshr i32 %masked, 8
  %sum = add i32 %masked, %high
  ret i32 %sum
}

; Do not replace the range test with a long chain of one-bit shifts.
define i32 @single_bit(i32 %x) {
; MIN-LABEL: single_bit:
; MIN-NOT: srli
; MIN: sltu
; MIN-NOT: srli
; MIN: ret
  %masked = and i32 %x, 512
  %bit = lshr i32 %masked, 9
  ret i32 %bit
}

; Min's existing size helper remains preferable for long constant shifts.
define i32 @size_helper(i32 %x) minsize {
; MIN-LABEL: size_helper:
; MIN-NOT: srli
; MIN: __riscc_lshrsi12
  %masked = and i32 %x, 61440
  %nibble = lshr i32 %masked, 12
  ret i32 %nibble
}
