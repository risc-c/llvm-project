; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=nano -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s

; Global optimization can shrink an integer flag to i1. All extending loads
; must select a byte load, with the appropriate one-bit extension.
; CHECK-LABEL: zero_extend_flag:
; CHECK: ldb
define i16 @zero_extend_flag(ptr %p) {
  %v = load i1, ptr %p
  %r = zext i1 %v to i16
  ret i16 %r
}

; CHECK-LABEL: sign_extend_flag:
; CHECK: ldb
; CHECK: sub
define i16 @sign_extend_flag(ptr %p) {
  %v = load i1, ptr %p
  %r = sext i1 %v to i16
  ret i16 %r
}

; CHECK-LABEL: wide_zero_extend_flag:
; CHECK: ldb
define i32 @wide_zero_extend_flag(ptr %p) {
  %v = load i1, ptr %p
  %r = zext i1 %v to i32
  ret i32 %r
}

; CHECK-LABEL: wide_sign_extend_flag:
; CHECK: ldb
; CHECK: sub
define i32 @wide_sign_extend_flag(ptr %p) {
  %v = load i1, ptr %p
  %r = sext i1 %v to i32
  ret i32 %r
}
