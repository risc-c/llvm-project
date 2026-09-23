; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=sys -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -O0 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=DEBUG --implicit-check-not=_fast

; Speed builds enter the shared fixed-count shifts through computed dispatch.
define i32 @left(i32 %value, i32 %count) {
; CHECK-LABEL: left:
; CHECK: __riscc_shlsi_fast
; DEBUG-LABEL: left:
; DEBUG: __ashlsi3
  %r = shl i32 %value, %count
  ret i32 %r
}

define i32 @right(i32 %value, i32 %count) {
; CHECK-LABEL: right:
; CHECK: __riscc_lshrsi_fast
; DEBUG-LABEL: right:
; DEBUG: __lshrsi3
  %r = lshr i32 %value, %count
  ret i32 %r
}

define i32 @signed_right(i32 %value, i32 %count) {
; CHECK-LABEL: signed_right:
; CHECK: __riscc_ashrsi_fast
; DEBUG-LABEL: signed_right:
; DEBUG: __ashrsi3
  %r = ashr i32 %value, %count
  ret i32 %r
}

; Both size modes retain the small loops instead of linking all 31 rungs.
define i32 @small_left(i32 %value, i32 %count) optsize {
; CHECK-LABEL: small_left:
; CHECK: __ashlsi3
  %r = shl i32 %value, %count
  ret i32 %r
}

define i32 @small_right(i32 %value, i32 %count) minsize {
; CHECK-LABEL: small_right:
; CHECK: __lshrsi3
  %r = lshr i32 %value, %count
  ret i32 %r
}

define i32 @small_signed_right(i32 %value, i32 %count) optsize {
; CHECK-LABEL: small_signed_right:
; CHECK: __ashrsi3
  %r = ashr i32 %value, %count
  ret i32 %r
}

define i32 @unoptimized_left(i32 %value, i32 %count) noinline optnone {
; CHECK-LABEL: unoptimized_left:
; CHECK: __ashlsi3
  %r = shl i32 %value, %count
  ret i32 %r
}
