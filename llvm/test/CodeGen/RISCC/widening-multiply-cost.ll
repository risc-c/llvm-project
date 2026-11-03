; REQUIRES: riscc-registered-target
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size -disable-output %s 2>&1 | FileCheck %s --check-prefix=SIZE
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency -disable-output %s 2>&1 | FileCheck %s --check-prefix=LAT16
; RUN: sed -e 's/i32/i64/g' -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size -disable-output - 2>&1 | FileCheck %s --check-prefix=SIZE
; RUN: sed -e 's/i32/i64/g' -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency -disable-output - 2>&1 | FileCheck %s --check-prefix=LAT32

target datalayout = "e-p:16:16"

; Constant widening products expand into native half-word products and carries,
; rather than a single instruction. This estimate also recognizes masked operands.
define i32 @unsigned_product(i16 %x) {
; SIZE-LABEL: function 'unsigned_product'
; SIZE: cost of 20 for instruction: %p = mul
; LAT16-LABEL: function 'unsigned_product'
; LAT16: cost of 28 for instruction: %p = mul
; LAT32-LABEL: function 'unsigned_product'
; LAT32: cost of 20 for instruction: %p = mul
  %wide = zext i16 %x to i32
  %p = mul i32 %wide, 23457
  ret i32 %p
}

define i32 @signed_product(i16 %x) {
; SIZE-LABEL: function 'signed_product'
; SIZE: cost of 20 for instruction: %p = mul
; LAT16-LABEL: function 'signed_product'
; LAT16: cost of 28 for instruction: %p = mul
; LAT32-LABEL: function 'signed_product'
; LAT32: cost of 20 for instruction: %p = mul
  %wide = sext i16 %x to i32
  %p = mul i32 23457, %wide
  ret i32 %p
}

; Arbitrary wide inputs still use a shared helper, as do size functions.
define i32 @wide_product(i32 %x) {
; SIZE-LABEL: function 'wide_product'
; SIZE: cost of 1 for instruction: %p = mul
; LAT16-LABEL: function 'wide_product'
; LAT16: cost of 1 for instruction: %p = mul
; LAT32-LABEL: function 'wide_product'
; LAT32: cost of 1 for instruction: %p = mul
  %p = mul i32 %x, 23457
  ret i32 %p
}

define i32 @size_product(i16 %x) optsize {
; SIZE-LABEL: function 'size_product'
; SIZE: cost of 1 for instruction: %p = mul
; LAT16-LABEL: function 'size_product'
; LAT16: cost of 1 for instruction: %p = mul
; LAT32-LABEL: function 'size_product'
; LAT32: cost of 1 for instruction: %p = mul
  %wide = zext i16 %x to i32
  %p = mul i32 %wide, 23457
  ret i32 %p
}

; Cheap shift/add products retain their existing cost.
define i32 @small_product(i16 %x) {
; SIZE-LABEL: function 'small_product'
; SIZE: cost of 1 for instruction: %p = mul
; LAT16-LABEL: function 'small_product'
; LAT16: cost of 1 for instruction: %p = mul
; LAT32-LABEL: function 'small_product'
; LAT32: cost of 1 for instruction: %p = mul
  %wide = zext i16 %x to i32
  %p = mul i32 %wide, 7
  ret i32 %p
}
