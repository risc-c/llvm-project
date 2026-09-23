; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc -mcpu=nano -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=NANO

define i32 @merge_sign32(i32 %a, i32 %b) {
; RC32-LABEL: merge_sign32:
; RC32: ldpc
; RC32-NOT: ldpc
; RC32: xor
; RC32: and
; RC32: xor
; RC32-NOT: ldpc
; RC32: ret
  %low = and i32 %a, 2147483647
  %high = and i32 %b, -2147483648
  %r = or i32 %low, %high
  ret i32 %r
}

define i32 @merge_byte32(i32 %a, i32 %b) {
; RC32-LABEL: merge_byte32:
; RC32-NOT: ldpc
; RC32: xor
; RC32: andi {{r[0-7]}}, 255
; RC32: xor
; RC32: ret
  %low = and i32 %a, 255
  %high = and i32 %b, -256
  %r = or i32 %low, %high
  ret i32 %r
}

define i16 @merge_sign16(i16 %a, i16 %b) {
; RC16-LABEL: merge_sign16:
; RC16-DAG: lui {{r[0-7]}}, 128
; RC16-DAG: xor
; RC16-NOT: ori
; RC16: and
; RC16: xor
; RC16: ret
  %low = and i16 %a, 32767
  %high = and i16 %b, -32768
  %r = or i16 %low, %high
  ret i16 %r
}

define i16 @merge_byte16(i16 %a, i16 %b) {
; RC16-LABEL: merge_byte16:
; RC16-NOT: lui
; RC16: xor
; RC16: andi {{r[0-7]}}, 255
; RC16: xor
; RC16: ret
; NANO-LABEL: merge_byte16:
; NANO-NOT: lui
; NANO: xor
; NANO: andi {{r[0-7]}}, 255
; NANO: xor
  %low = and i16 %a, 255
  %high = and i16 %b, -256
  %r = or i16 %low, %high
  ret i16 %r
}

; Overlapping masks do not form a bit selection.
define i32 @overlap(i32 %a, i32 %b) {
; RC32-LABEL: overlap:
; RC32-NOT: xor
; RC32: or
; RC32: ret
  %low = and i32 %a, 65535
  %high = and i32 %b, -256
  %r = or i32 %low, %high
  ret i32 %r
}

; A separately used masked operand must stay available.
define i32 @multiple_uses(i32 %a, i32 %b, ptr %p) {
; RC32-LABEL: multiple_uses:
; RC32-NOT: xor
; RC32: st
; RC32-NOT: xor
; RC32: ret
  %low = and i32 %a, 65535
  store volatile i32 %low, ptr %p
  %high = and i32 %b, -65536
  %r = or i32 %low, %high
  ret i32 %r
}
