; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32,+mdu -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=MDU
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=SYS

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-f32:32-f64:32-a:8:32-n8:16:32-S32"
target triple = "riscc-none-elf"

; Full without MULHU can pass two native-width arguments to the compact
; widening helpers. MDU, Min, and Sys retain their existing wide paths.
define i64 @u_ext(i32 %a, i32 %b) {
; FULL-LABEL: u_ext:
; FULL:       __riscc_umulwide
; MDU-LABEL:  u_ext:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  u_ext:
; MIN:        __muldi3
; SYS-LABEL:  u_ext:
; SYS:        __muldi3
  %za = zext i32 %a to i64
  %zb = zext i32 %b to i64
  %p = mul i64 %za, %zb
  ret i64 %p
}

define i64 @s_ext(i32 %a, i32 %b) {
; FULL-LABEL: s_ext:
; FULL:       __riscc_smulwide
; MDU-LABEL:  s_ext:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  s_ext:
; MIN:        __muldi3
; SYS-LABEL:  s_ext:
; SYS:        __muldi3
  %sa = sext i32 %a to i64
  %sb = sext i32 %b to i64
  %p = mul i64 %sa, %sb
  ret i64 %p
}

; Known zero upper halves need not come from an explicit zext.
define i64 @u_mask(i64 %a, i64 %b) {
; FULL-LABEL: u_mask:
; FULL:       __riscc_umulwide
; MDU-LABEL:  u_mask:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  u_mask:
; MIN:        __muldi3
; SYS-LABEL:  u_mask:
; SYS:        __muldi3
  %aa = and i64 %a, 4294967295
  %bb = and i64 %b, 4294967295
  %p = mul i64 %aa, %bb
  ret i64 %p
}

; A general full-width product must keep the existing helper on software-wide
; paths.
define i64 @full_width(i64 %a, i64 %b) {
; FULL-LABEL: full_width:
; FULL:       __muldi3
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; MDU-LABEL:  full_width:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  full_width:
; MIN:        __muldi3
; SYS-LABEL:  full_width:
; SYS:        __muldi3
  %p = mul i64 %a, %b
  ret i64 %p
}

; Only one narrow operand is insufficient for the native widening helper.
define i64 @mixed_unsigned(i32 %a, i64 %b) {
; FULL-LABEL: mixed_unsigned:
; FULL:       __muldi3
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; MDU-LABEL:  mixed_unsigned:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  mixed_unsigned:
; MIN:        __muldi3
; SYS-LABEL:  mixed_unsigned:
; SYS:        __muldi3
  %za = zext i32 %a to i64
  %p = mul i64 %za, %b
  ret i64 %p
}

; A signed narrow operand mixed with a full-width unsigned operand is kept
; on the general helper path.
define i64 @mixed_signed_unsigned(i32 %a, i64 %b) {
; FULL-LABEL: mixed_signed_unsigned:
; FULL:       __muldi3
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; MDU-LABEL:  mixed_signed_unsigned:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  mixed_signed_unsigned:
; MIN:        __muldi3
; SYS-LABEL:  mixed_signed_unsigned:
; SYS:        __muldi3
  %sa = sext i32 %a to i64
  %p = mul i64 %sa, %b
  ret i64 %p
}

; These boundary values fit the native unsigned and signed domains.
define i64 @u_fitting_constant(i32 %a) {
; FULL-LABEL: u_fitting_constant:
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; FULL:       ret s7
; MDU-LABEL:  u_fitting_constant:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  u_fitting_constant:
; MIN-NOT:    __muldi3
; MIN:        ret s7
; SYS-LABEL:  u_fitting_constant:
; SYS-NOT:    __muldi3
; SYS:        ret s7
  %za = zext i32 %a to i64
  %p = mul i64 %za, 4294967295
  ret i64 %p
}

define i64 @s_fitting_constant(i32 %a) {
; FULL-LABEL: s_fitting_constant:
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; FULL:       ret s7
; MDU-LABEL:  s_fitting_constant:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  s_fitting_constant:
; MIN:        __muldi3
; SYS-LABEL:  s_fitting_constant:
; SYS:        __muldi3
  %sa = sext i32 %a to i64
  %p = mul i64 %sa, -2147483646
  ret i64 %p
}

; Values just outside the native domains must not be narrowed.
define i64 @u_nonfitting_constant(i32 %a) {
; FULL-LABEL: u_nonfitting_constant:
; FULL:       __muldi3
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; MDU-LABEL:  u_nonfitting_constant:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  u_nonfitting_constant:
; MIN:        __muldi3
; SYS-LABEL:  u_nonfitting_constant:
; SYS:        __muldi3
  %za = zext i32 %a to i64
  %p = mul i64 %za, 4294967299
  ret i64 %p
}

define i64 @s_nonfitting_constant(i32 %a) {
; FULL-LABEL: s_nonfitting_constant:
; FULL:       __muldi3
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; MDU-LABEL:  s_nonfitting_constant:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  s_nonfitting_constant:
; MIN:        __muldi3
; SYS-LABEL:  s_nonfitting_constant:
; SYS-NOT:    __riscc_umulwide
; SYS-NOT:    __riscc_smulwide
  %sa = sext i32 %a to i64
  %p = mul i64 %sa, 2147483650
  ret i64 %p
}

; The native-width operation itself remains a hardware multiply on profiles
; that provide MUL, independent of the widening helper selection above.
define i32 @native_mul(i32 %a, i32 %b) {
; FULL-LABEL: native_mul:
; FULL:       {{[[:space:]]mul[[:space:]]}}
; MDU-LABEL:  native_mul:
; MDU:        {{[[:space:]]mul[[:space:]]}}
; MIN-LABEL:  native_mul:
; MIN:        __mulsi3
; SYS-LABEL:  native_mul:
; SYS:        __mulsi3
  %p = mul i32 %a, %b
  ret i32 %p
}

; Size functions continue to share a widening helper for arbitrary constants.
define i64 @s_constant_size(i32 %a) optsize {
; FULL-LABEL: s_constant_size:
; FULL:       __riscc_smulwide
; MDU-LABEL:  s_constant_size:
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  s_constant_size:
; MIN:        __muldi3
; SYS-LABEL:  s_constant_size:
; SYS:        __muldi3
  %sa = sext i32 %a to i64
  %p = mul i64 %sa, 12345
  ret i64 %p
}

; Equal native operands use a one-argument, three-partial-product helper.
define i64 @u_square(i32 %a) {
; FULL-LABEL: u_square:
; FULL:       __riscc_usquarewide
; MDU-LABEL:  u_square:
; MDU-NOT:    __riscc_usquarewide
; MIN-LABEL:  u_square:
; MIN:        __muldi3
; SYS-LABEL:  u_square:
; SYS:        __muldi3
  %wide = zext i32 %a to i64
  %square = mul i64 %wide, %wide
  ret i64 %square
}

; Equal native operands use a one-argument, three-partial-product helper.
define i64 @u_square_size(i32 %a) optsize {
; FULL-LABEL: u_square_size:
; FULL:       __riscc_usquarewide
; MDU-LABEL:  u_square_size:
; MDU-NOT:    __riscc_usquarewide
; MIN-LABEL:  u_square_size:
; MIN:        __muldi3
; SYS-LABEL:  u_square_size:
; SYS:        __muldi3
  %wide = zext i32 %a to i64
  %square = mul i64 %wide, %wide
  ret i64 %square
}

; Equal native operands use a one-argument, three-partial-product helper.
define i64 @s_square(i32 %a) {
; FULL-LABEL: s_square:
; FULL:       __riscc_ssquarewide
; MDU-LABEL:  s_square:
; MDU-NOT:    __riscc_ssquarewide
; MIN-LABEL:  s_square:
; MIN:        __muldi3
; SYS-LABEL:  s_square:
; SYS:        __muldi3
  %wide = sext i32 %a to i64
  %square = mul i64 %wide, %wide
  ret i64 %square
}

; Equal native operands use a one-argument, three-partial-product helper.
define i64 @s_square_size(i32 %a) optsize {
; FULL-LABEL: s_square_size:
; FULL:       __riscc_ssquarewide
; MDU-LABEL:  s_square_size:
; MDU-NOT:    __riscc_ssquarewide
; MIN-LABEL:  s_square_size:
; MIN:        __muldi3
; SYS-LABEL:  s_square_size:
; SYS:        __muldi3
  %wide = sext i32 %a to i64
  %square = mul i64 %wide, %wide
  ret i64 %square
}
