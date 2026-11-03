; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s
; RUN: sed -e 's/i32/i16/g' -e 's/i64/i32/g' %s | llc -mtriple=riscc -mcpu=full -verify-machineinstrs | FileCheck %s

; The incoming R1/R2 values already have the commutative helper's ABI order.
; CHECK-LABEL: unsigned_product:
; CHECK-NOT: mov
; CHECK: __riscc_umulwide
define i64 @unsigned_product(i32 %a, i32 %b) {
  %wide_a = zext i32 %a to i64
  %wide_b = zext i32 %b to i64
  %result = mul i64 %wide_a, %wide_b
  ret i64 %result
}

; CHECK-LABEL: signed_product:
; CHECK-NOT: mov
; CHECK: __riscc_smulwide
define i64 @signed_product(i32 %a, i32 %b) optsize {
  %wide_a = sext i32 %a to i64
  %wide_b = sext i32 %b to i64
  %result = mul i64 %wide_a, %wide_b
  ret i64 %result
}
