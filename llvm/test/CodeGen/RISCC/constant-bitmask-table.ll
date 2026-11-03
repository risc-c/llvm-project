; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,RC32
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=full -verify-machineinstrs | FileCheck %s --check-prefixes=CHECK,RC16
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=min -verify-machineinstrs | FileCheck %s --check-prefixes=CHECK,RC16
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=nano -verify-machineinstrs | FileCheck %s --check-prefix=NANO

; Both widths share byte masks; all possible indices must be in bounds.
; CHECK-LABEL: left_mask:
; RC16: __riscc_bitmask_l8
; CHECK: ldb
; CHECK-NOT: bnez
; CHECK: ret
; RC32: .long __riscc_bitmask_l8
; NANO-LABEL: left_mask:
; NANO-NOT: __riscc_bitmask
; NANO-NOT: ldb
; NANO: .size left_mask,
; NANO-NOT: __riscc_bitmask
define i32 @left_mask(i32 %n) {
  %count = and i32 %n, 7
  %r = shl i32 1, %count
  ret i32 %r
}

; CHECK-LABEL: right_mask:
; RC16: __riscc_bitmask_r8
; CHECK: ldb
; CHECK: ret
; RC32: .long __riscc_bitmask_r8
define i32 @right_mask(i32 %n) optsize {
  %count = and i32 %n, 7
  %r = lshr i32 128, %count
  ret i32 %r
}

; A higher initial left bit offsets the table index. The count can be 0, 2, 4 or 6.
; CHECK-LABEL: offset_left:
; CHECK: ldb
; CHECK: ret
define i32 @offset_left(i32 %n) {
  %count = and i32 %n, 6
  %r = shl i32 2, %count
  ret i32 %r
}

; A lower initial right bit offsets the index in the opposite table.
; CHECK-LABEL: offset_right:
; CHECK: ldb
; CHECK: ret
define i32 @offset_right(i32 %n) {
  %count = and i32 %n, 5
  %r = lshr i32 32, %count
  ret i32 %r
}

; Do not read beyond the table, introduce tables for short counts, or grow Oz.
; CHECK-LABEL: larger_count:
; CHECK-NOT: ldb
; CHECK: ret
define i32 @larger_count(i32 %n) {
  %count = and i32 %n, 7
  %r = shl i32 2, %count
  ret i32 %r
}

; CHECK-LABEL: short_count:
; CHECK-NOT: ldb
; CHECK: ret
define i32 @short_count(i32 %n) {
  %count = and i32 %n, 3
  %r = shl i32 1, %count
  ret i32 %r
}

; CHECK-LABEL: minimum_size:
; CHECK-NOT: ldb
; CHECK: .size minimum_size,
define i32 @minimum_size(i32 %n) minsize {
  %count = and i32 %n, 7
  %r = shl i32 1, %count
  ret i32 %r
}

; CHECK-LABEL: arbitrary_constant:
; CHECK-NOT: ldb
; CHECK: ret
define i32 @arbitrary_constant(i32 %n) {
  %count = and i32 %n, 7
  %r = shl i32 3, %count
  ret i32 %r
}
