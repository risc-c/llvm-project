; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC32

; A whole-word shift becomes register moves during type legalization. These
; products need only that shift and one wide add/subtract, including optsize.
define i32 @minus16(i32 %x) {
; RC16-LABEL: minus16:
; RC16-NOT: jall
; RC16: sub
; RC16-NOT: jall
; RC16: ret s7
  %p = mul i32 %x, 65535
  ret i32 %p
}

define i32 @plus16(i32 %x) optsize {
; RC16-LABEL: plus16:
; RC16-NOT: jall
; RC16: add
; RC16-NOT: jall
; RC16: ret s7
  %p = mul i32 %x, 65537
  ret i32 %p
}

define i32 @negative_minus16(i32 %x) {
; RC16-LABEL: negative_minus16:
; RC16-NOT: jall
; RC16: sub
; RC16-NOT: jall
; RC16: ret s7
  %p = mul i32 %x, -65535
  ret i32 %p
}

define i64 @minus32(i64 %x) {
; RC32-LABEL: minus32:
; RC32-NOT: jalr
; RC32: sub
; RC32-NOT: jalr
; RC32: ret s7
  %p = mul i64 %x, 4294967295
  ret i64 %p
}

define i64 @plus32(i64 %x) optsize {
; RC32-LABEL: plus32:
; RC32-NOT: jalr
; RC32: add
; RC32-NOT: jalr
; RC32: ret s7
  %p = mul i64 %x, 4294967297
  ret i64 %p
}

define i64 @negative_minus32(i64 %x) {
; RC32-LABEL: negative_minus32:
; RC32-NOT: jalr
; RC32: sub
; RC32-NOT: jalr
; RC32: ret s7
  %p = mul i64 %x, -4294967295
  ret i64 %p
}
