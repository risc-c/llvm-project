; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,RC32
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=full -verify-machineinstrs | FileCheck %s --check-prefixes=CHECK,RC16
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=nano -verify-machineinstrs | FileCheck %s --check-prefix=NANO

; CHECK-LABEL: extract_bit:
; RC16: __riscc_bitmask_l8
; CHECK: ldb
; CHECK: and
; CHECK-NOT: xor
; CHECK: sltu
; CHECK: ret
; RC32: .long __riscc_bitmask_l8
; NANO-LABEL: extract_bit:
; NANO-NOT: __riscc_bitmask
; NANO-NOT: ldb
; NANO: .size extract_bit,
define i32 @extract_bit(i32 %x, i32 %n) {
  %count = and i32 %n, 7
  %shift = lshr i32 %x, %count
  %result = and i32 %shift, 1
  ret i32 %result
}

; CHECK-LABEL: signed_extract:
; CHECK: ldb
; CHECK: and
; CHECK: sltu
; CHECK: ret
define i32 @signed_extract(i32 %x, i32 %n) optsize {
  %count = and i32 %n, 7
  %shift = ashr i32 %x, %count
  %result = and i32 %shift, 1
  ret i32 %result
}

; CHECK-LABEL: short_count:
; CHECK-NOT: ldb
; CHECK: ret
define i32 @short_count(i32 %x, i32 %n) {
  %count = and i32 %n, 3
  %shift = lshr i32 %x, %count
  %result = and i32 %shift, 1
  ret i32 %result
}

; A range ending at four does not amortize the lookup and normalization.
; CHECK-LABEL: two_counts:
; CHECK-NOT: ldb
; CHECK: ret
define i32 @two_counts(i32 %x, i32 %n) {
  %count = and i32 %n, 4
  %shift = lshr i32 %x, %count
  %result = and i32 %shift, 1
  ret i32 %result
}

; CHECK-LABEL: minimum_size:
; CHECK-NOT: ldb
; CHECK: .size minimum_size,
define i32 @minimum_size(i32 %x, i32 %n) minsize {
  %count = and i32 %n, 7
  %shift = lshr i32 %x, %count
  %result = and i32 %shift, 1
  ret i32 %result
}

; CHECK-LABEL: nonbit_result:
; CHECK-NOT: ldb
; CHECK: ret
define i32 @nonbit_result(i32 %x, i32 %n) {
  %count = and i32 %n, 7
  %shift = lshr i32 %x, %count
  %result = and i32 %shift, 3
  ret i32 %result
}

; CHECK-LABEL: shared_shift:
; CHECK-NOT: ldb
; CHECK: ret
define i32 @shared_shift(i32 %x, i32 %n, ptr %out) {
  %count = and i32 %n, 7
  %shift = lshr i32 %x, %count
  store i32 %shift, ptr %out
  %result = and i32 %shift, 1
  ret i32 %result
}
