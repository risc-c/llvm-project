; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s

target triple = "riscc-none-elf"
target datalayout = "e-m:e-p:32:32-i64:32-n32-S32"

declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i32 @llvm.fshr.i32(i32, i32, i32)

define i32 @rotate_left(i32 %value, i32 %count)  {
; CHECK-LABEL: rotate_left:
; CHECK:       andi r{{[0-6]}}, 31
; CHECK:       __riscc_rotlsi_fast
  %result = call i32 @llvm.fshl.i32(i32 %value, i32 %value, i32 %count)
  ret i32 %result
}

define i32 @rotate_right(i32 %value, i32 %count)  {
; CHECK-LABEL: rotate_right:
; CHECK:       andi r{{[0-6]}}, 31
; CHECK:       __riscc_rotlsi_fast
  %result = call i32 @llvm.fshr.i32(i32 %value, i32 %value, i32 %count)
  ret i32 %result
}

define i32 @rotate_left_size(i32 %value, i32 %count) optsize {
; CHECK-LABEL: rotate_left_size:
; CHECK:       andi r{{[0-6]}}, 31
; CHECK:       __riscc_rotlsi
  %result = call i32 @llvm.fshl.i32(i32 %value, i32 %value, i32 %count)
  ret i32 %result
}

define i32 @rotate_right_size(i32 %value, i32 %count) minsize optsize {
; CHECK-LABEL: rotate_right_size:
; CHECK:       andi r{{[0-6]}}, 31
; CHECK:       __riscc_rotlsi
  %result = call i32 @llvm.fshr.i32(i32 %value, i32 %value, i32 %count)
  ret i32 %result
}

; Reassociation must not hide an add-of-rotate behind an unrelated add.
define i32 @add_rotate(i32 %acc, i32 %value, i32 %count) {
; CHECK-LABEL: add_rotate:
; CHECK:       __riscc_rotlsi_fast
  %left = shl i32 %value, %count
  %inverse = sub i32 32, %count
  %right = lshr i32 %value, %inverse
  %sum = add i32 %left, %acc
  %result = add i32 %sum, %right
  ret i32 %result
}

define i32 @add_rotate_right(i32 %acc, i32 %value, i32 %count) {
; CHECK-LABEL: add_rotate_right:
; CHECK:       __riscc_rotlsi_fast
  %right = lshr i32 %value, %count
  %inverse = sub i32 32, %count
  %left = shl i32 %value, %inverse
  %sum = add i32 %right, %acc
  %result = add i32 %sum, %left
  ret i32 %result
}

; A different complement or shifted value must retain both shifts.
define i32 @not_rotate(i32 %acc, i32 %x, i32 %y, i32 %count) {
; CHECK-LABEL: not_rotate:
; CHECK-NOT:   __riscc_rotl
  %left = shl i32 %x, %count
  %inverse = sub i32 32, %count
  %right = lshr i32 %y, %inverse
  %sum = add i32 %left, %acc
  %result = add i32 %sum, %right
  ret i32 %result
}
