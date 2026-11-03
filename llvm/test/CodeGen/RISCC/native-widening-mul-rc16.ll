; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+mdu -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=MDU
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=SYS

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:32-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

; Full without MULHU can pass two native-width arguments to the compact
; widening helpers. MDU, Min, and Sys retain their existing wide paths.
define i32 @u_ext(i16 %a, i16 %b) {
; FULL-LABEL: u_ext:
; FULL:       __riscc_umulwide
; MDU-LABEL:  u_ext:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  u_ext:
; MIN:        __mulsi3
; SYS-LABEL:  u_ext:
; SYS:        __mulsi3
  %za = zext i16 %a to i32
  %zb = zext i16 %b to i32
  %p = mul i32 %za, %zb
  ret i32 %p
}

define i32 @s_ext(i16 %a, i16 %b) {
; FULL-LABEL: s_ext:
; FULL:       __riscc_smulwide
; MDU-LABEL:  s_ext:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  s_ext:
; MIN:        __mulsi3
; SYS-LABEL:  s_ext:
; SYS:        __mulsi3
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %p = mul i32 %sa, %sb
  ret i32 %p
}

; Known zero upper halves need not come from an explicit zext.
define i32 @u_mask(i32 %a, i32 %b) {
; FULL-LABEL: u_mask:
; FULL:       __riscc_umulwide
; MDU-LABEL:  u_mask:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  u_mask:
; MIN:        __mulsi3
; SYS-LABEL:  u_mask:
; SYS:        __mulsi3
  %aa = and i32 %a, 65535
  %bb = and i32 %b, 65535
  %p = mul i32 %aa, %bb
  ret i32 %p
}

; A general full-width product must keep the existing helper.
define i32 @full_width(i32 %a, i32 %b) {
; FULL-LABEL: full_width:
; FULL:       __mulsi3
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; MDU-LABEL:  full_width:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  full_width:
; MIN:        __mulsi3
; SYS-LABEL:  full_width:
; SYS:        __mulsi3
  %p = mul i32 %a, %b
  ret i32 %p
}

; Only one narrow operand is insufficient for the native widening helper.
define i32 @mixed_unsigned(i16 %a, i32 %b) {
; FULL-LABEL: mixed_unsigned:
; FULL:       __mulsi3
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; MDU-LABEL:  mixed_unsigned:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  mixed_unsigned:
; MIN:        __mulsi3
; SYS-LABEL:  mixed_unsigned:
; SYS:        __mulsi3
  %za = zext i16 %a to i32
  %p = mul i32 %za, %b
  ret i32 %p
}

; A signed narrow operand mixed with a full-width unsigned operand is kept
; on the general helper path.
define i32 @mixed_signed_unsigned(i16 %a, i32 %b) {
; FULL-LABEL: mixed_signed_unsigned:
; FULL:       __mulsi3
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; MDU-LABEL:  mixed_signed_unsigned:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  mixed_signed_unsigned:
; MIN:        __mulsi3
; SYS-LABEL:  mixed_signed_unsigned:
; SYS:        __mulsi3
  %sa = sext i16 %a to i32
  %p = mul i32 %sa, %b
  ret i32 %p
}

; These boundary values fit the native unsigned and signed domains.
define i32 @u_fitting_constant(i16 %a) {
; FULL-LABEL: u_fitting_constant:
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; FULL:       ret s7
; MDU-LABEL:  u_fitting_constant:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  u_fitting_constant:
; MIN-NOT:    __mulsi3
; MIN:        ret s7
; SYS-LABEL:  u_fitting_constant:
; SYS-NOT:    __mulsi3
; SYS:        ret s7
  %za = zext i16 %a to i32
  %p = mul i32 %za, 65535
  ret i32 %p
}

define i32 @s_fitting_constant(i16 %a) {
; FULL-LABEL: s_fitting_constant:
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; FULL:       ret s7
; MDU-LABEL:  s_fitting_constant:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  s_fitting_constant:
; MIN:        __mulsi3
; SYS-LABEL:  s_fitting_constant:
; SYS:        __mulsi3
  %sa = sext i16 %a to i32
  %p = mul i32 %sa, -32766
  ret i32 %p
}

; Values just outside the native domains must not be narrowed. The odd
; constants avoid the small shift/add constant-multiply combines.
define i32 @u_nonfitting_constant(i16 %a) {
; FULL-LABEL: u_nonfitting_constant:
; FULL:       __mulsi3
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; MDU-LABEL:  u_nonfitting_constant:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  u_nonfitting_constant:
; MIN:        __mulsi3
; SYS-LABEL:  u_nonfitting_constant:
; SYS:        __mulsi3
  %za = zext i16 %a to i32
  %p = mul i32 %za, 65538
  ret i32 %p
}

define i32 @s_nonfitting_constant(i16 %a) {
; FULL-LABEL: s_nonfitting_constant:
; FULL:       __mulsi3
; FULL-NOT:   __riscc_umulwide
; FULL-NOT:   __riscc_smulwide
; MDU-LABEL:  s_nonfitting_constant:
; MDU-NOT:    __riscc_umulwide
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  s_nonfitting_constant:
; MIN:        __mulsi3
; SYS-LABEL:  s_nonfitting_constant:
; SYS:        __mulsi3
  %sa = sext i16 %a to i32
  %p = mul i32 %sa, 32770
  ret i32 %p
}

; The native-width operation itself remains a hardware multiply on profiles
; that provide MUL, independent of the widening helper selection above.
define i16 @native_mul(i16 %a, i16 %b) {
; FULL-LABEL: native_mul:
; FULL:       {{[[:space:]]mul[[:space:]]}}
; MDU-LABEL:  native_mul:
; MDU:        {{[[:space:]]mul[[:space:]]}}
; MIN-LABEL:  native_mul:
; MIN:        __mulhi3
; SYS-LABEL:  native_mul:
; SYS:        __mulhi3
  %p = mul i16 %a, %b
  ret i16 %p
}

; Size functions continue to share a widening helper for arbitrary constants.
define i32 @s_constant_size(i16 %a) optsize {
; FULL-LABEL: s_constant_size:
; FULL:       __riscc_smulwide
; MDU-LABEL:  s_constant_size:
; MDU-NOT:    __riscc_smulwide
; MIN-LABEL:  s_constant_size:
; MIN:        __mulsi3
; SYS-LABEL:  s_constant_size:
; SYS:        __mulsi3
  %sa = sext i16 %a to i32
  %p = mul i32 %sa, 12345
  ret i32 %p
}

; Equal native operands use a one-argument, three-partial-product helper.
define i32 @u_square(i16 %a) {
; FULL-LABEL: u_square:
; FULL:       __riscc_usquarewide
; MDU-LABEL:  u_square:
; MDU-NOT:    __riscc_usquarewide
; MIN-LABEL:  u_square:
; MIN:        __mulsi3
; SYS-LABEL:  u_square:
; SYS:        __mulsi3
  %wide = zext i16 %a to i32
  %square = mul i32 %wide, %wide
  ret i32 %square
}

; Equal native operands use a one-argument, three-partial-product helper.
define i32 @u_square_size(i16 %a) optsize {
; FULL-LABEL: u_square_size:
; FULL:       __riscc_usquarewide
; MDU-LABEL:  u_square_size:
; MDU-NOT:    __riscc_usquarewide
; MIN-LABEL:  u_square_size:
; MIN:        __mulsi3
; SYS-LABEL:  u_square_size:
; SYS:        __mulsi3
  %wide = zext i16 %a to i32
  %square = mul i32 %wide, %wide
  ret i32 %square
}

; Equal native operands use a one-argument, three-partial-product helper.
define i32 @s_square(i16 %a) {
; FULL-LABEL: s_square:
; FULL:       __riscc_ssquarewide
; MDU-LABEL:  s_square:
; MDU-NOT:    __riscc_ssquarewide
; MIN-LABEL:  s_square:
; MIN:        __mulsi3
; SYS-LABEL:  s_square:
; SYS:        __mulsi3
  %wide = sext i16 %a to i32
  %square = mul i32 %wide, %wide
  ret i32 %square
}

; Equal native operands use a one-argument, three-partial-product helper.
define i32 @s_square_size(i16 %a) optsize {
; FULL-LABEL: s_square_size:
; FULL:       __riscc_ssquarewide
; MDU-LABEL:  s_square_size:
; MDU-NOT:    __riscc_ssquarewide
; MIN-LABEL:  s_square_size:
; MIN:        __mulsi3
; SYS-LABEL:  s_square_size:
; SYS:        __mulsi3
  %wide = sext i16 %a to i32
  %square = mul i32 %wide, %wide
  ret i32 %square
}
