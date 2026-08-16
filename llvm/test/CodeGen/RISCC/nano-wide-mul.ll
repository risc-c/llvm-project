; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -verify-machineinstrs < %s | FileCheck %s

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

declare { i16, i1 } @llvm.umul.with.overflow.i16(i16, i16)

; Integer type legalization expands this intrinsic through __mulsi3. Its two
; i32 operands must remain whole ABI arguments: r1:r2, then stack.
define i1 @overflow(i16 %a, i16 %b) {
; CHECK-LABEL: overflow:
; CHECK:       st r2, [r7 + 0]
; CHECK-NEXT:  ldi r2, 0
; CHECK-NEXT:  st r2, [r7 + 2]
; CHECK:       ldi16 r0, __mulsi3
; CHECK-NEXT:  jalr r6, r0
  %result = call { i16, i1 } @llvm.umul.with.overflow.i16(i16 %a, i16 %b)
  %overflow = extractvalue { i16, i1 } %result, 1
  ret i1 %overflow
}
