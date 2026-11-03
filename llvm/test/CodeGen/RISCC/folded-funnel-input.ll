; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s
; RUN: sed -e 's/i32/i16/g' -e 's/1073741824/16384/g' -e 's/31/15/g' -e 's/30/14/g' %s | llc -mtriple=riscc -mcpu=full -verify-machineinstrs | FileCheck %s
; RUN: sed -e 's/i32/i16/g' -e 's/1073741824/16384/g' -e 's/31/15/g' -e 's/30/14/g' %s | llc -mtriple=riscc -mcpu=min -verify-machineinstrs | FileCheck %s

; Constructing an incoming-word shift may already return a folded constant.
; Do not invoke shift lowering on that constant as if it had shift operands.
declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i32 @llvm.fshr.i32(i32, i32, i32)

; CHECK-LABEL: constant_incoming_left:
; CHECK: fsl1
; CHECK: fsl1
; CHECK: ret
define i32 @constant_incoming_left(i32 %v) {
  %r = call i32 @llvm.fshl.i32(i32 %v, i32 1073741824, i32 2)
  ret i32 %r
}

; CHECK-LABEL: constant_incoming_right:
; CHECK: fsr1
; CHECK: fsr1
; CHECK: ret
define i32 @constant_incoming_right(i32 %v) {
  %r = call i32 @llvm.fshr.i32(i32 2, i32 %v, i32 2)
  ret i32 %r
}

; CHECK-LABEL: constant_incoming_inverse:
; CHECK: fsr1
; CHECK: fsr1
; CHECK: ret
define i32 @constant_incoming_inverse(i32 %v) {
  %r = call i32 @llvm.fshl.i32(i32 2, i32 %v, i32 30)
  ret i32 %r
}

; General split shifts can also fold an incoming constant.
; CHECK-LABEL: constant_incoming_general:
; CHECK: ret
define i32 @constant_incoming_general(i32 %v) {
  %r = call i32 @llvm.fshl.i32(i32 %v, i32 1073741824, i32 5)
  ret i32 %r
}
