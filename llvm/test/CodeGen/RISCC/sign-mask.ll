; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefixes=FAST,SIZE16
; RUN: sed -e 's/i16/i32/g' -e 's/, 15/, 31/g' %s | llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs -o - | FileCheck %s --check-prefixes=FAST,SIZE32
; RUN: llc -mtriple=riscc -mcpu=min -verify-machineinstrs %s -o - | FileCheck %s --check-prefixes=FAST,SMALL
; RUN: sed -e 's/i16/i32/g' -e 's/, 15/, 31/g' %s | llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs -o - | FileCheck %s --check-prefixes=FAST,SMALL

; A native sign mask uses no iterative shift in speed code. The constant
; zero is shared by the compare and negation. INT_MIN remains well defined.
define i16 @sign_mask(i16 %x) {
; FAST-LABEL: sign_mask:
; FAST: ldi [[ZERO:r[0-6]]], 0
; FAST: slt [[BIT:r[0-6]]], r1, [[ZERO]]
; FAST-NEXT: sub r1, [[ZERO]], [[BIT]]
; FAST-NOT: srai
; FAST: ret
  %mask = ashr i16 %x, 15
  ret i16 %mask
}

define i16 @small_sign_mask(i16 %x) optsize {
; SIZE16-LABEL: small_sign_mask:
; SIZE16: srai [[TMP:r[0-6]]], r1, 8
; SIZE16-NEXT: srai r1, [[TMP]], 7
; SIZE16: ret
; SIZE32-LABEL: small_sign_mask:
; SIZE32-COUNT-3: srai {{.*}}, 8
; SIZE32-NEXT: srai {{.*}}, 7
; SIZE32: ret
; SMALL-LABEL: small_sign_mask:
; SMALL: slt
; SMALL: sub
; SMALL-NOT: srai
; SMALL: ret
  %mask = ashr i16 %x, 15
  ret i16 %mask
}

define i16 @tiny_sign_mask(i16 %x) minsize optsize {
; SIZE16-LABEL: tiny_sign_mask:
; SIZE16: srai [[TMP:r[0-6]]], r1, 8
; SIZE16-NEXT: srai r1, [[TMP]], 7
; SIZE16: ret
; SIZE32-LABEL: tiny_sign_mask:
; SIZE32-COUNT-3: srai {{.*}}, 8
; SIZE32-NEXT: srai {{.*}}, 7
; SIZE32: ret
; SMALL-LABEL: tiny_sign_mask:
; SMALL: slt
; SMALL: sub
; SMALL-NOT: srai
; SMALL: ret
  %mask = ashr i16 %x, 15
  ret i16 %mask
}
