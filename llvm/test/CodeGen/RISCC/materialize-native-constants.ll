; REQUIRES: riscc-registered-target
; RUN: split-file %s %t
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -stop-after=finalize-isel %t/rc16.ll -o - | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -stop-after=finalize-isel %t/rc32.ll -o - | FileCheck %s --check-prefix=RC32

; Direct native-width constants use the compact u8 seed at 255, the RC16
; upper-byte seed at 256, and the full-width fallback outside those ranges.

; RC16-LABEL: name: rc16_ldi_boundary
; RC16:       %0:gpr = LDI 255
; RC16-LABEL: name: rc16_lui_boundary
; RC16:       %0:gpr = LUI 1
; RC16-LABEL: name: rc16_full_width
; RC16:       %0:gpr = LDI16

; RC32-LABEL: name: rc32_ldi_boundary
; RC32:       %0:gpr32 = LDI32 255
; RC32-LABEL: name: rc32_pool_boundary
; RC32:       %0:gpr32 = LDPC

;--- rc16.ll
target triple = "riscc-none-elf"

define i16 @rc16_ldi_boundary() {
  ret i16 255
}

define i16 @rc16_lui_boundary() {
  ret i16 256
}

define i16 @rc16_full_width() {
  ret i16 65535
}

;--- rc32.ll
target triple = "riscc-none-elf"

define i32 @rc32_ldi_boundary() {
  ret i32 255
}

define i32 @rc32_pool_boundary() {
  ret i32 256
}
