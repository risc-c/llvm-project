; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC16

declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i32 @llvm.fshr.i32(i32, i32, i32)
declare i16 @llvm.fshr.i16(i16, i16, i16)

define i32 @left_two(i32 %high, i32 %low) minsize {
; RC32-LABEL: left_two:
; RC32: fsl1
; RC32-NOT: srli
; RC32: fsl1
; RC32: ret
  %a = shl i32 %high, 2
  %b = lshr i32 %low, 30
  %r = or i32 %a, %b
  ret i32 %r
}

define i32 @right_two(i32 %high, i32 %low) minsize {
; RC32-LABEL: right_two:
; RC32: fsr1
; RC32: srli {{r[0-7]}}, {{r[0-7]}}, 1
; RC32: fsr1
; RC32: ret
  %r = call i32 @llvm.fshr.i32(i32 %high, i32 %low, i32 2)
  ret i32 %r
}

define i32 @left_thirty(i32 %high, i32 %low) {
; RC32-LABEL: left_thirty:
; RC32: fsr1
; RC32: fsr1
; RC32: ret
  %r = call i32 @llvm.fshl.i32(i32 %high, i32 %low, i32 30)
  ret i32 %r
}

define i32 @right_thirty(i32 %high, i32 %low) {
; RC32-LABEL: right_thirty:
; RC32: fsl1
; RC32: fsl1
; RC32: ret
  %r = call i32 @llvm.fshr.i32(i32 %high, i32 %low, i32 30)
  ret i32 %r
}

define i16 @left_two16(i16 %high, i16 %low) minsize {
; RC16-LABEL: left_two16:
; RC16: fsl1
; RC16-NOT: srli
; RC16: fsl1
; RC16: ret
  %a = shl i16 %high, 2
  %b = lshr i16 %low, 14
  %r = or i16 %a, %b
  ret i16 %r
}

define i16 @right_fourteen16(i16 %high, i16 %low) minsize {
; RC16-LABEL: right_fourteen16:
; RC16: fsl1
; RC16: fsl1
; RC16: ret
  %r = call i16 @llvm.fshr.i16(i16 %high, i16 %low, i16 14)
  ret i16 %r
}
