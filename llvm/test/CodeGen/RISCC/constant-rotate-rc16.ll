; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -verify-machineinstrs < %s | FileCheck %s

target triple = "riscc-none-elf"
target datalayout = "e-m:e-p:16:16-i64:16-n16-S16"

declare i16 @llvm.fshl.i16(i16, i16, i16)
declare i16 @llvm.fshr.i16(i16, i16, i16)

define i16 @left_two(i16 %x)  {
; CHECK-LABEL: left_two:
; CHECK: fsl1 r1, r1
; CHECK-NEXT: fsl1 r1, r1
; CHECK-NEXT: ret s7
  %r = call i16 @llvm.fshl.i16(i16 %x, i16 %x, i16 2)
  ret i16 %r
}

define i16 @right_three(i16 %x)  {
; CHECK-LABEL: right_three:
; CHECK: fsr1 r1, r1
; CHECK-NEXT: fsr1 r1, r1
; CHECK-NEXT: fsr1 r1, r1
; CHECK-NEXT: ret s7
  %r = call i16 @llvm.fshr.i16(i16 %x, i16 %x, i16 3)
  ret i16 %r
}

define i16 @left_inverse_two_size(i16 %x) optsize {
; CHECK-LABEL: left_inverse_two_size:
; CHECK: fsr1 r1, r1
; CHECK-NEXT: fsr1 r1, r1
; CHECK-NEXT: ret s7
  %r = call i16 @llvm.fshl.i16(i16 %x, i16 %x, i16 14)
  ret i16 %r
}

define i16 @right_inverse_three_min(i16 %x) minsize optsize {
; CHECK-LABEL: right_inverse_three_min:
; CHECK: fsl1 r1, r1
; CHECK-NEXT: fsl1 r1, r1
; CHECK-NEXT: fsl1 r1, r1
; CHECK-NEXT: ret s7
  %r = call i16 @llvm.fshr.i16(i16 %x, i16 %x, i16 13)
  ret i16 %r
}

; Distinct source words still need to shift the incoming endpoint bit.
define i16 @funnel_two(i16 %x, i16 %y) {
; CHECK-LABEL: funnel_two:
; CHECK: fsl1
; CHECK: {{slli|add}}
; CHECK: fsl1
; CHECK: ret s7
  %r = call i16 @llvm.fshl.i16(i16 %x, i16 %y, i16 2)
  ret i16 %r
}

; Preserve a tie when the source has another use; partial shifts may be shared.
define i16 @rotate_shared_input(i16 %x) {
; FULL-LABEL: rotate_shared_input:
; FULL-NOT: fsr1
; FULL: srli
; FULL: or
; FULL: ret s7
  %r = call i16 @llvm.fshr.i16(i16 %x, i16 %x, i16 4)
  %result = xor i16 %r, %x
  ret i16 %result
}
