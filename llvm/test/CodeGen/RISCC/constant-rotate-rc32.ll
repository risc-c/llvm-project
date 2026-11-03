; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s

target triple = "riscc-none-elf"
target datalayout = "e-m:e-p:32:32-i64:32-n32-S32"

declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i32 @llvm.fshr.i32(i32, i32, i32)

define i32 @left_two(i32 %x)  {
; CHECK-LABEL: left_two:
; CHECK: fsl1 r1, r1
; CHECK-NEXT: fsl1 r1, r1
; CHECK-NEXT: ret s7
  %r = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 2)
  ret i32 %r
}

define i32 @right_three(i32 %x)  {
; CHECK-LABEL: right_three:
; CHECK: fsr1 r1, r1
; CHECK-NEXT: fsr1 r1, r1
; CHECK-NEXT: fsr1 r1, r1
; CHECK-NEXT: ret s7
  %r = call i32 @llvm.fshr.i32(i32 %x, i32 %x, i32 3)
  ret i32 %r
}

define i32 @left_inverse_two_size(i32 %x) optsize {
; CHECK-LABEL: left_inverse_two_size:
; CHECK: fsr1 r1, r1
; CHECK-NEXT: fsr1 r1, r1
; CHECK-NEXT: ret s7
  %r = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 30)
  ret i32 %r
}

define i32 @right_inverse_three_min(i32 %x) minsize optsize {
; CHECK-LABEL: right_inverse_three_min:
; CHECK: fsl1 r1, r1
; CHECK-NEXT: fsl1 r1, r1
; CHECK-NEXT: fsl1 r1, r1
; CHECK-NEXT: ret s7
  %r = call i32 @llvm.fshr.i32(i32 %x, i32 %x, i32 29)
  ret i32 %r
}

; Distinct source words still need to shift the incoming endpoint bit.
define i32 @funnel_two(i32 %x, i32 %y) {
; CHECK-LABEL: funnel_two:
; CHECK: fsl1
; CHECK: {{slli|add}}
; CHECK: fsl1
; CHECK: ret s7
  %r = call i32 @llvm.fshl.i32(i32 %x, i32 %y, i32 2)
  ret i32 %r
}

; Preserve a tie when the source has another use; partial shifts may be shared.
define i32 @rotate_shared_input(i32 %x) {
; FULL-LABEL: rotate_shared_input:
; FULL-NOT: fsr1
; FULL: srli
; FULL: or
; FULL: ret s7
  %r = call i32 @llvm.fshr.i32(i32 %x, i32 %x, i32 6)
  %result = xor i32 %r, %x
  ret i32 %result
}
