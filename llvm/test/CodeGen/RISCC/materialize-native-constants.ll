; REQUIRES: riscc-registered-target
; RUN: split-file %s %t
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -stop-after=finalize-isel %t/rc16.ll -o - | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -stop-after=finalize-isel %t/rc32.ll -o - | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -verify-machineinstrs %t/rc32.ll -o - | FileCheck %s --check-prefix=RC32BASE
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -mattr=+rc32 -verify-machineinstrs %t/rc32.ll -o - | FileCheck %s --check-prefix=RC32BASE
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs %t/rc32.ll -o - | FileCheck %s --check-prefix=RC32FULL

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

; A two-instruction constant can double a u8 seed. SLLI is Full-only;
; Min/Sys must use ADD even when the shift count is one.
; RC32BASE-LABEL: rc32_double_seed:
; RC32BASE: ldi r1, 192
; RC32BASE-NEXT: add r1, r1, r1
; RC32BASE-NEXT: ret
; RC32FULL-LABEL: rc32_double_seed:
; RC32FULL: ldi r1, 192
; RC32FULL-NEXT: slli r1, r1, 1
; RC32FULL-NEXT: ret

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

define i32 @rc32_double_seed() optsize {
  ret i32 384
}
