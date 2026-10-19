; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s

@end = external global i8

; A negative global displacement is signed, not an oversized unsigned integer.
define i8 @previous_byte() {
; CHECK-LABEL: previous_byte:
; CHECK: ldb
  %p = getelementptr i8, ptr @end, i16 -4
  %v = load i8, ptr %p
  ret i8 %v
}

define ptr @previous_pointer() {
; CHECK-LABEL: previous_pointer:
; CHECK: addi {{.*}}-4
  %p = getelementptr i8, ptr @end, i16 -4
  ret ptr %p
}
