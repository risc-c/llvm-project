; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=MAIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=MAIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=MAIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=MAIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=NANO

target triple = "riscc-none-elf"

declare i16 @llvm.fshl.i16(i16, i16, i16)
declare i16 @llvm.fshr.i16(i16, i16, i16)
declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i64 @llvm.fshl.i64(i64, i64, i64)

define i16 @funnel_left_one(i16 %high, i16 %low) {
; MAIN-LABEL: funnel_left_one:
; MAIN:       fsl1 r1, r1, r2
; NANO-LABEL: funnel_left_one:
; NANO-NOT:   fsl1
  %result = call i16 @llvm.fshl.i16(i16 %high, i16 %low, i16 1)
  ret i16 %result
}

define i16 @funnel_right_one(i16 %high, i16 %low) {
; MAIN-LABEL: funnel_right_one:
; MAIN:       fsr1 r1, r2, r1
; NANO-LABEL: funnel_right_one:
; NANO-NOT:   fsr1
  %result = call i16 @llvm.fshr.i16(i16 %high, i16 %low, i16 1)
  ret i16 %result
}

define i16 @funnel_left_seventeen(i16 %high, i16 %low) {
; MAIN-LABEL: funnel_left_seventeen:
; MAIN:       fsl1 r1, r1, r2
; NANO-LABEL: funnel_left_seventeen:
; NANO-NOT:   fsl1
  %result = call i16 @llvm.fshl.i16(i16 %high, i16 %low, i16 17)
  ret i16 %result
}

define i16 @funnel_left_two(i16 %high, i16 %low) {
; MAIN-LABEL: funnel_left_two:
; MAIN-NOT:   fsl1
; MAIN-NOT:   fsr1
; NANO-LABEL: funnel_left_two:
; NANO-NOT:   fsl1
; NANO-NOT:   fsr1
  %result = call i16 @llvm.fshl.i16(i16 %high, i16 %low, i16 2)
  ret i16 %result
}

define i16 @funnel_right_two(i16 %high, i16 %low) {
; MAIN-LABEL: funnel_right_two:
; MAIN-NOT:   fsl1
; MAIN-NOT:   fsr1
; NANO-LABEL: funnel_right_two:
; NANO-NOT:   fsl1
; NANO-NOT:   fsr1
  %result = call i16 @llvm.fshr.i16(i16 %high, i16 %low, i16 2)
  ret i16 %result
}

define i16 @funnel_left_zero(i16 %high, i16 %low) {
; MAIN-LABEL: funnel_left_zero:
; MAIN-NOT:   fsl1
; MAIN-NOT:   fsr1
; NANO-LABEL: funnel_left_zero:
; NANO-NOT:   fsl1
; NANO-NOT:   fsr1
  %result = call i16 @llvm.fshl.i16(i16 %high, i16 %low, i16 0)
  ret i16 %result
}

define i16 @funnel_left_sixteen(i16 %high, i16 %low) {
; MAIN-LABEL: funnel_left_sixteen:
; MAIN-NOT:   fsl1
; MAIN-NOT:   fsr1
; NANO-LABEL: funnel_left_sixteen:
; NANO-NOT:   fsl1
; NANO-NOT:   fsr1
  %result = call i16 @llvm.fshl.i16(i16 %high, i16 %low, i16 16)
  ret i16 %result
}

define i16 @funnel_left_fifteen_minsize(i16 %high, i16 %low) minsize {
; MAIN-LABEL: funnel_left_fifteen_minsize:
; MAIN:       fsr1 r1, r2, r1
; NANO-LABEL: funnel_left_fifteen_minsize:
; NANO-NOT:   fsr1
  %result = call i16 @llvm.fshl.i16(i16 %high, i16 %low, i16 15)
  ret i16 %result
}

define i16 @funnel_right_fifteen_minsize(i16 %high, i16 %low) minsize {
; MAIN-LABEL: funnel_right_fifteen_minsize:
; MAIN:       fsl1 r1, r1, r2
; NANO-LABEL: funnel_right_fifteen_minsize:
; NANO-NOT:   fsl1
  %result = call i16 @llvm.fshr.i16(i16 %high, i16 %low, i16 15)
  ret i16 %result
}

define i16 @funnel_right_seventeen(i16 %high, i16 %low) {
; MAIN-LABEL: funnel_right_seventeen:
; MAIN:       fsr1 r1, r2, r1
; NANO-LABEL: funnel_right_seventeen:
; NANO-NOT:   fsr1
  %result = call i16 @llvm.fshr.i16(i16 %high, i16 %low, i16 17)
  ret i16 %result
}

define i16 @funnel_left_variable(i16 %high, i16 %low, i16 %amount) {
; MAIN-LABEL: funnel_left_variable:
; MAIN-NOT:   fsl1
; MAIN-NOT:   fsr1
; NANO-LABEL: funnel_left_variable:
; NANO-NOT:   fsl1
; NANO-NOT:   fsr1
  %result = call i16 @llvm.fshl.i16(
      i16 %high, i16 %low, i16 %amount)
  ret i16 %result
}

define i16 @funnel_right_variable(i16 %high, i16 %low, i16 %amount) {
; MAIN-LABEL: funnel_right_variable:
; MAIN-NOT:   fsl1
; MAIN-NOT:   fsr1
; NANO-LABEL: funnel_right_variable:
; NANO-NOT:   fsl1
; NANO-NOT:   fsr1
  %result = call i16 @llvm.fshr.i16(
      i16 %high, i16 %low, i16 %amount)
  ret i16 %result
}

define i16 @left_shift_pair(i16 %high, i16 %low) {
; MAIN-LABEL: left_shift_pair:
; MAIN:       fsl1
; NANO-LABEL: left_shift_pair:
; NANO-NOT:   fsl1
  %shifted = shl i16 %high, 1
  %fill = lshr i16 %low, 15
  %result = or i16 %shifted, %fill
  ret i16 %result
}

define i16 @right_shift_pair(i16 %low, i16 %high) {
; MAIN-LABEL: right_shift_pair:
; MAIN:       fsr1
; NANO-LABEL: right_shift_pair:
; NANO-NOT:   fsr1
  %shifted = lshr i16 %low, 1
  %fill = shl i16 %high, 15
  %result = or i16 %shifted, %fill
  ret i16 %result
}

define i32 @shift_left_i32_one(i32 %value) {
; MAIN-LABEL: shift_left_i32_one:
; MAIN:       fsl1 r2, r2, r1
; MAIN-NEXT:  {{add|shli}} r1, r1
; NANO-LABEL: shift_left_i32_one:
; NANO-NOT:   fsl1
  %result = shl i32 %value, 1
  ret i32 %result
}

define i32 @shift_right_i32_one(i32 %value) {
; MAIN-LABEL: shift_right_i32_one:
; MAIN:       fsr1 r1, r1, r2
; MAIN-NEXT:  shri r2, r2, 1
; NANO-LABEL: shift_right_i32_one:
; NANO-NOT:   fsr1
  %result = lshr i32 %value, 1
  ret i32 %result
}

define i32 @shift_arithmetic_i32_one(i32 %value) {
; MAIN-LABEL: shift_arithmetic_i32_one:
; MAIN:       fsr1 r1, r1, r2
; MAIN-NEXT:  sari r2, r2, 1
; NANO-LABEL: shift_arithmetic_i32_one:
; NANO-NOT:   fsr1
  %result = ashr i32 %value, 1
  ret i32 %result
}

; Wider funnel nodes must pass through normal type legalization before the
; target combines their i16 limbs.
define i32 @funnel_left_i32_one(i32 %high, i32 %low) {
  %result = call i32 @llvm.fshl.i32(i32 %high, i32 %low, i32 1)
  ret i32 %result
}

define i64 @funnel_left_i64_one(i64 %high, i64 %low) {
  %result = call i64 @llvm.fshl.i64(i64 %high, i64 %low, i64 1)
  ret i64 %result
}
