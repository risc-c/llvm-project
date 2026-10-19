; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=sys -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=MIN
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32,-wide-shift -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefix=MIN
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32,+wide-shift -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=MIN

define i32 @half(i32 %x) {
; CHECK-LABEL: half:
; CHECK: slli r1, r1, 8
; CHECK-NEXT: slli r1, r1, 8
; CHECK-NEXT: srai r1, r1, 8
; CHECK-NEXT: srai r1, r1, 8
; CHECK-NEXT: ret
; MIN-LABEL: half:
; MIN: ldpc
; MIN: and
; MIN: xor
; MIN: sub
; MIN: ret
  %s = trunc i32 %x to i16
  %r = sext i16 %s to i32
  ret i32 %r
}

; The first shift need not overwrite the source when it is still live.
define i32 @half_and_original(i32 %x) {
; CHECK-LABEL: half_and_original:
; CHECK: slli [[TMP:r[0-6]]], r1, 8
; CHECK-NEXT: slli [[TMP]], [[TMP]], 8
; CHECK-NEXT: srai [[TMP]], [[TMP]], 8
; CHECK-NEXT: srai [[TMP]], [[TMP]], 8
; CHECK-NEXT: add r1,
; CHECK-NEXT: ret
  %s = trunc i32 %x to i16
  %r = sext i16 %s to i32
  %sum = add i32 %r, %x
  ret i32 %sum
}
