; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s
; RUN: sed -e 's/i32/i16/g' -e 's/i64/i32/g' %s | llc -mtriple=riscc -mcpu=full -verify-machineinstrs | FileCheck %s

; Only bit patterns greater than three may bypass the small-operand dispatch.
; CHECK-LABEL: unsigned_large:
; CHECK: __riscc_umulwide_large
define i64 @unsigned_large(i32 %a, i32 %b) optsize {
  %large = or i32 %b, 4
  %lhs = zext i32 %a to i64
  %rhs = zext i32 %large to i64
  %product = mul i64 %lhs, %rhs
  ret i64 %product
}

; CHECK-LABEL: signed_large:
; CHECK: __riscc_smulwide_large
define i64 @signed_large(i32 %a, i32 %b) optsize {
  %large = or i32 %b, 4
  %lhs = sext i32 %a to i64
  %rhs = sext i32 %large to i64
  %product = mul i64 %lhs, %rhs
  ret i64 %product
}

; Size-optimized constant multiplication still shares its arithmetic body.
; CHECK-LABEL: constant_large:
; CHECK: __riscc_smulwide_large
define i64 @constant_large(i32 %a) optsize {
  %lhs = sext i32 %a to i64
  %product = mul i64 %lhs, -12345
  ret i64 %product
}

; CHECK-LABEL: possibly_small:
; CHECK-NOT: __riscc_umulwide_large
; CHECK: __riscc_umulwide
; CHECK-NOT: __riscc_umulwide_large
define i64 @possibly_small(i32 %a, i32 %b) optsize {
  %nonzero = or i32 %b, 2
  %lhs = zext i32 %a to i64
  %rhs = zext i32 %nonzero to i64
  %product = mul i64 %lhs, %rhs
  ret i64 %product
}

; A square still has a cheaper three-partial-product helper.
; CHECK-LABEL: large_square:
; CHECK: __riscc_usquarewide
define i64 @large_square(i32 %a) optsize {
  %large = or i32 %a, 4
  %wide = zext i32 %large to i64
  %product = mul i64 %wide, %wide
  ret i64 %product
}
