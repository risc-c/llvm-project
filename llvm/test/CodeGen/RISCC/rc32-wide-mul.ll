; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-f32:32-f64:32-a:8:32-n8:16:32-S32"
target triple = "riscc-none-elf"

declare { i32, i1 } @llvm.umul.with.overflow.i32(i32, i32)

; Native overflow needs only the high half of a 32x32 product. Compute it
; from halfword products instead of making a full 64-bit multiplication call.
define i1 @overflow(i32 %a, i32 %b) {
; CHECK-LABEL: overflow:
; CHECK-NOT:   jalr
; CHECK:       mul
; CHECK:       mul
; CHECK:       mul
; CHECK:       mul
; CHECK-NOT:   jalr
; CHECK:       ret s7
  %result = call { i32, i1 } @llvm.umul.with.overflow.i32(i32 %a, i32 %b)
  %overflow = extractvalue { i32, i1 } %result, 1
  ret i1 %overflow
}

; Full-width products still share the runtime rather than duplicating the
; much larger software partial-product sequence at every call site.
define i64 @wide_product(i64 %a, i64 %b) {
; CHECK-LABEL: wide_product:
; CHECK:       __muldi3
  %product = mul i64 %a, %b
  ret i64 %product
}

define i64 @small_wide_product(i64 %a) optsize {
; CHECK-LABEL: small_wide_product:
; CHECK-NOT:   __muldi3
; CHECK:       add
; CHECK:       ret s7
  %product = mul i64 %a, 5
  ret i64 %product
}
