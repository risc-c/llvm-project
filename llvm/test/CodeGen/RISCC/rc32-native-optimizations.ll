; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32,+mdu -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=MDU

target triple = "riscc-none-elf"

define i32 @multiply_by_3(i32 %value) {
; MIN-LABEL: multiply_by_3:
; MIN:       add [[TWICE_MIN:r[0-7]]], r1, r1
; MIN-NEXT:  add r1, [[TWICE_MIN]], r1
; FULL-LABEL: multiply_by_3:
; FULL:       add [[TWICE_FULL:r[0-7]]], r1, r1
; FULL-NEXT:  add r1, [[TWICE_FULL]], r1
  %product = mul i32 %value, 3
  ret i32 %product
}

define i32 @multiply_by_5(i32 %value) {
; MIN-LABEL: multiply_by_5:
; MIN:       add [[TWICE5:r[0-7]]], r1, r1
; MIN-NEXT:  add [[QUAD5:r[0-7]]], [[TWICE5]], [[TWICE5]]
; MIN-NEXT:  add r1, [[QUAD5]], r1
; FULL-LABEL: multiply_by_5:
; FULL:       add [[TWICE_FULL5:r[0-7]]], r1, r1
; FULL-NEXT:  add [[QUAD_FULL5:r[0-7]]], [[TWICE_FULL5]], [[TWICE_FULL5]]
; FULL-NEXT:  add r1, [[QUAD_FULL5]], r1
  %product = mul i32 %value, 5
  ret i32 %product
}

; Size-oriented hardware builds retain the shorter load/multiply pair.
define i32 @multiply_by_5_small(i32 %value) optsize {
; FULL-LABEL: multiply_by_5_small:
; FULL:       ldi [[CONST5:r[0-7]]], 5
; FULL-NEXT:  mul r1, r1, [[CONST5]]
  %product = mul i32 %value, 5
  ret i32 %product
}

; Subtracting from a power of two also handles negative multipliers without
; a separate negation. Min uses doubling for the shift.
define i32 @multiply_by_minus_3(i32 %value) {
; MIN-LABEL: multiply_by_minus_3:
; MIN:       add [[TWICE_NEG:r[0-7]]], r1, r1
; MIN-NEXT:  add [[QUAD_NEG:r[0-7]]], [[TWICE_NEG]], [[TWICE_NEG]]
; MIN-NEXT:  sub r1, r1, [[QUAD_NEG]]
; FULL-LABEL: multiply_by_minus_3:
; FULL:       slli [[FOUR:r[0-7]]], r1, 2
; FULL-NEXT:  sub r1, r1, [[FOUR]]
  %product = mul i32 %value, -3
  ret i32 %product
}

define i32 @select_or_zero(i1 %condition, i32 %value) {
; MIN-LABEL: select_or_zero:
; MIN:       andi r1, 1
; MIN-NEXT:  ldi r0, 0
; MIN-NEXT:  sub r0, r0, r1
; MIN-NEXT:  and r1, r2, r0
; FULL-LABEL: select_or_zero:
; FULL:       andi r1, 1
; FULL-NEXT:  ldi r0, 0
; FULL-NEXT:  sub r0, r0, r1
; FULL-NEXT:  and r1, r2, r0
  %result = select i1 %condition, i32 %value, i32 0
  ret i32 %result
}

declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i32 @llvm.fshr.i32(i32, i32, i32)

define i32 @funnel_left_one(i32 %high, i32 %low) {
; MIN-LABEL: funnel_left_one:
; MIN:       fsl1 r1, r2
; FULL-LABEL: funnel_left_one:
; FULL:       fsl1 r1, r2
  %result = call i32 @llvm.fshl.i32(i32 %high, i32 %low, i32 1)
  ret i32 %result
}

define i32 @funnel_left_thirty_one(i32 %high, i32 %low) {
; MIN-LABEL: funnel_left_thirty_one:
; MIN:       fsr1 r2, r1
; MIN-NEXT:  mov r1, r2
; FULL-LABEL: funnel_left_thirty_one:
; FULL:       fsr1 r2, r1
; FULL-NEXT:  mov r1, r2
  %result = call i32 @llvm.fshl.i32(i32 %high, i32 %low, i32 31)
  ret i32 %result
}

define i32 @funnel_right_one(i32 %high, i32 %low) {
; MIN-LABEL: funnel_right_one:
; MIN:       fsr1 r2, r1
; MIN-NEXT:  mov r1, r2
; FULL-LABEL: funnel_right_one:
; FULL:       fsr1 r2, r1
; FULL-NEXT:  mov r1, r2
  %result = call i32 @llvm.fshr.i32(i32 %high, i32 %low, i32 1)
  ret i32 %result
}

define i32 @funnel_right_thirty_one(i32 %high, i32 %low) {
; MIN-LABEL: funnel_right_thirty_one:
; MIN:       fsl1 r1, r2
; FULL-LABEL: funnel_right_thirty_one:
; FULL:       fsl1 r1, r2
  %result = call i32 @llvm.fshr.i32(i32 %high, i32 %low, i32 31)
  ret i32 %result
}

define i32 @funnel_left_variable(i32 %high, i32 %low, i32 %amount) {
; MIN-LABEL: funnel_left_variable:
; MIN:       andi {{r[0-7]}}, 31
; FULL-LABEL: funnel_left_variable:
; FULL:       andi {{r[0-7]}}, 31
  %result = call i32 @llvm.fshl.i32(i32 %high, i32 %low, i32 %amount)
  ret i32 %result
}

define i32 @funnel_right_variable(i32 %high, i32 %low, i32 %amount) {
; MIN-LABEL: funnel_right_variable:
; MIN:       andi {{r[0-7]}}, 31
; FULL-LABEL: funnel_right_variable:
; FULL:       andi {{r[0-7]}}, 31
  %result = call i32 @llvm.fshr.i32(i32 %high, i32 %low, i32 %amount)
  ret i32 %result
}

define i32 @unsigned_pair(i32 %left, i32 %right, ptr %out) {
; MIN-LABEL: unsigned_pair:
; MIN:       __udivmodsi4
; FULL-LABEL: unsigned_pair:
; FULL:       __udivmodsi4
; MDU-LABEL: unsigned_pair:
; MDU:       ldi r0, 0
; MDU-NEXT:  divu r0, r1, r2
; MDU-NEXT:  st r0, [r3 + 0]
; MDU-NOT:   __udivmodsi4
  %quotient = udiv i32 %left, %right
  %remainder = urem i32 %left, %right
  store i32 %remainder, ptr %out, align 4
  ret i32 %quotient
}

define i32 @unsigned_reconstructed_pair(i32 %left, i32 %right, ptr %out) {
; MIN-LABEL: unsigned_reconstructed_pair:
; MIN:       __udivmodsi4
; FULL-LABEL: unsigned_reconstructed_pair:
; FULL:       __udivmodsi4
; MDU-LABEL: unsigned_reconstructed_pair:
; MDU:       ldi r0, 0
; MDU-NEXT:  divu r0, r1, r2
; MDU-NEXT:  st r0, [r3 + 0]
; MDU-NOT:   __udivmodsi4
  %quotient = udiv i32 %left, %right
  %product = mul i32 %quotient, %right
  %remainder = sub i32 %left, %product
  store i32 %remainder, ptr %out, align 4
  ret i32 %quotient
}

define i32 @signed_pair(i32 %left, i32 %right, ptr %out) {
; MIN-LABEL: signed_pair:
; MIN:       __divmodsi4
; FULL-LABEL: signed_pair:
; FULL:       __divmodsi4
  %quotient = sdiv i32 %left, %right
  %remainder = srem i32 %left, %right
  store i32 %remainder, ptr %out, align 4
  ret i32 %quotient
}

define i32 @signed_reconstructed_pair(i32 %left, i32 %right, ptr %out) {
; MIN-LABEL: signed_reconstructed_pair:
; MIN:       __divmodsi4
; FULL-LABEL: signed_reconstructed_pair:
; FULL:       __divmodsi4
  %quotient = sdiv i32 %left, %right
  %product = mul i32 %quotient, %right
  %remainder = sub i32 %left, %product
  store i32 %remainder, ptr %out, align 4
  ret i32 %quotient
}
