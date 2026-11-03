; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=nano -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,SOFT
; RUN: llc -mtriple=riscc -mcpu=min -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,SOFT
; RUN: llc -mtriple=riscc -mcpu=sys -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,SOFT
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,SOFT
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+mdu -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,MDU
; RUN: sed 's/i16/i32/g' %s | llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs | FileCheck %s --check-prefixes=CHECK,SOFT
; RUN: sed 's/i16/i32/g' %s | llc -mtriple=riscc -mcpu=sys -mattr=+rc32 -verify-machineinstrs | FileCheck %s --check-prefixes=CHECK,SOFT
; RUN: sed 's/i16/i32/g' %s | llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs | FileCheck %s --check-prefixes=CHECK,SOFT
; RUN: sed 's/i16/i32/g' %s | llc -mtriple=riscc -mcpu=full -mattr=+rc32,+mdu -verify-machineinstrs | FileCheck %s --check-prefixes=CHECK,MDU

; A native quotient/remainder pair uses one register-return helper. Its second
; result can go straight to the user's pointer, without a temporary stack slot.
define i16 @unsigned_pair(i16 %n, i16 %d, ptr %r) optsize {
; CHECK-LABEL: unsigned_pair:
; SOFT-DAG:   __riscc_udivmod{{hi|si}}
; SOFT-DAG:   st r2,
; SOFT-NOT:   __udiv
; SOFT-NOT:   __umod
; MDU:        divu
; MDU-NOT:    __riscc_udivmod
  %q = udiv i16 %n, %d
  %rem = urem i16 %n, %d
  store i16 %rem, ptr %r
  ret i16 %q
}

define i16 @signed_pair(i16 %n, i16 %d, ptr %r) optsize {
; CHECK-LABEL: signed_pair:
; CHECK-DAG:   __riscc_divmod{{hi|si}}
; CHECK-DAG:   st r2,
; CHECK-NOT:   __div
; CHECK-NOT:   __mod
  %q = sdiv i16 %n, %d
  %rem = srem i16 %n, %d
  store i16 %rem, ptr %r
  ret i16 %q
}

; Recover the same pair after generic combining reconstructs the remainder.
define i16 @signed_reconstructed(i16 %n, i16 %d, ptr %r) {
; CHECK-LABEL: signed_reconstructed:
; CHECK-DAG:   __riscc_divmod{{hi|si}}
; CHECK-DAG:   st r2,
; CHECK-NOT:   __mul
  %q = sdiv i16 %n, %d
  %p = mul i16 %q, %d
  %rem = sub i16 %n, %p
  store i16 %rem, ptr %r
  ret i16 %q
}

; A single result keeps its single-result entry.
define i16 @signed_quotient(i16 %n, i16 %d) optsize {
; CHECK-LABEL: signed_quotient:
; CHECK-NOT:   __riscc_divmod
; CHECK:       __div{{hi|si}}3
  %q = sdiv i16 %n, %d
  ret i16 %q
}

define i16 @signed_remainder(i16 %n, i16 %d) optsize {
; CHECK-LABEL: signed_remainder:
; CHECK-NOT:   __riscc_divmod
; CHECK:       __mod{{hi|si}}3
  %r = srem i16 %n, %d
  ret i16 %r
}

; Pair formation must retain the inexpensive constant-divisor expansion.
define i16 @positive_power_of_two(i16 %n, ptr %r) optsize {
; CHECK-LABEL: positive_power_of_two:
; CHECK-NOT:   __div
; CHECK-NOT:   __riscc_divmod
; CHECK:       srai
; CHECK-NOT:   __div
; CHECK-NOT:   __riscc_divmod
  %q = sdiv i16 %n, 4
  %rem = srem i16 %n, 4
  store i16 %rem, ptr %r
  ret i16 %q
}

define i16 @negative_power_of_two(i16 %n, ptr %r) optsize {
; CHECK-LABEL: negative_power_of_two:
; CHECK-NOT:   __div
; CHECK-NOT:   __riscc_divmod
; CHECK:       srai
; CHECK-NOT:   __div
; CHECK-NOT:   __riscc_divmod
  %q = sdiv i16 %n, -4
  %rem = srem i16 %n, -4
  store i16 %rem, ptr %r
  ret i16 %q
}
