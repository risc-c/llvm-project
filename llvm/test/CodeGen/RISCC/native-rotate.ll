; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full  -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=min  -verify-machineinstrs < %s | FileCheck %s

target triple = "riscc-none-elf"
target datalayout = "e-m:e-p:16:16-i64:16-n16-S16"

declare i16 @llvm.fshl.i16(i16, i16, i16)
declare i16 @llvm.fshr.i16(i16, i16, i16)

define i16 @rotate_left(i16 %value, i16 %count)  {
; CHECK-LABEL: rotate_left:
; CHECK:       andi r{{[0-6]}}, 15
; CHECK:       __riscc_rotlhi_fast
  %result = call i16 @llvm.fshl.i16(i16 %value, i16 %value, i16 %count)
  ret i16 %result
}

define i16 @rotate_right(i16 %value, i16 %count)  {
; CHECK-LABEL: rotate_right:
; CHECK:       andi r{{[0-6]}}, 15
; CHECK:       __riscc_rotlhi_fast
  %result = call i16 @llvm.fshr.i16(i16 %value, i16 %value, i16 %count)
  ret i16 %result
}

define i16 @rotate_left_size(i16 %value, i16 %count) optsize {
; CHECK-LABEL: rotate_left_size:
; CHECK:       andi r{{[0-6]}}, 15
; CHECK:       __riscc_rotlhi
  %result = call i16 @llvm.fshl.i16(i16 %value, i16 %value, i16 %count)
  ret i16 %result
}

define i16 @rotate_right_size(i16 %value, i16 %count) minsize optsize {
; CHECK-LABEL: rotate_right_size:
; CHECK:       andi r{{[0-6]}}, 15
; CHECK:       __riscc_rotlhi
  %result = call i16 @llvm.fshr.i16(i16 %value, i16 %value, i16 %count)
  ret i16 %result
}

; Reassociation must not hide an add-of-rotate behind an unrelated add.
define i16 @add_rotate(i16 %acc, i16 %value, i16 %count) {
; CHECK-LABEL: add_rotate:
; CHECK:       __riscc_rotlhi_fast
  %left = shl i16 %value, %count
  %inverse = sub i16 16, %count
  %right = lshr i16 %value, %inverse
  %sum = add i16 %left, %acc
  %result = add i16 %sum, %right
  ret i16 %result
}

define i16 @add_rotate_right(i16 %acc, i16 %value, i16 %count) {
; CHECK-LABEL: add_rotate_right:
; CHECK:       __riscc_rotlhi_fast
  %right = lshr i16 %value, %count
  %inverse = sub i16 16, %count
  %left = shl i16 %value, %inverse
  %sum = add i16 %right, %acc
  %result = add i16 %sum, %left
  ret i16 %result
}

; A different complement or shifted value must retain both shifts.
define i16 @not_rotate(i16 %acc, i16 %x, i16 %y, i16 %count) {
; CHECK-LABEL: not_rotate:
; CHECK-NOT:   __riscc_rotl
  %left = shl i16 %x, %count
  %inverse = sub i16 16, %count
  %right = lshr i16 %y, %inverse
  %sum = add i16 %left, %acc
  %result = add i16 %sum, %right
  ret i16 %result
}
