; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -verify-machineinstrs < %s | FileCheck %s --check-prefixes=NANO,SMALL
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -verify-machineinstrs < %s | FileCheck %s --check-prefixes=MIN,SMALL
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -verify-machineinstrs < %s | FileCheck %s --check-prefixes=SYS,SMALL
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefixes=FULL,WIDE

target triple = "riscc-none-elf"

declare i16 @callee(i16)

define i16 @multiply(i16 %a, i16 %b) {
; NANO-LABEL: multiply:
; NANO:       ldi16 r0, __mulhi3
; NANO-NEXT:  jalr r6, r0
; NANO:       jalr r0, r{{[0-6]}}
; MIN-LABEL: multiply:
; MIN:       ldi16 r0, __mulhi3
; MIN-NEXT:  jalr s7, r0
; SYS-LABEL: multiply:
; SYS:       jall s7, __mulhi3
; FULL-LABEL: multiply:
; FULL:       mul
  %result = mul i16 %a, %b
  ret i16 %result
}

define i16 @shift_left_five(i16 %value) {
; NANO-LABEL: shift_left_five:
; NANO:       add
; NANO-NOT:   slli
; NANO:       jalr r0, r6
; MIN-LABEL: shift_left_five:
; MIN:       add
; MIN-NOT:   slli
; FULL-LABEL: shift_left_five:
; FULL:       slli
  %result = shl i16 %value, 5
  ret i16 %result
}

define i16 @shift_right_five(i16 %value) {
; NANO-LABEL: shift_right_five:
; NANO:       srli {{.*}}, 1
; NANO-NOT:   srli {{.*}}, 5
; NANO:       jalr r0, r6
; MIN-LABEL: shift_right_five:
; MIN:       srli {{.*}}, 1
; MIN-NOT:   srli {{.*}}, 5
; FULL-LABEL: shift_right_five:
; FULL:       srli {{.*}}, 5
  %result = lshr i16 %value, 5
  ret i16 %result
}

define i16 @shift_left_eleven_size(i16 %value) minsize {
; SMALL-LABEL: shift_left_eleven_size:
; NANO:        ldi16 r0, __riscc_shlhi11
; NANO-NEXT:   jalr r6, r0
; MIN:         ldi16 r0, __riscc_shlhi11
; MIN-NEXT:    jalr s7, r0
; SYS:         jall s7, __riscc_shlhi11
; WIDE-LABEL:  shift_left_eleven_size:
; WIDE:        slli [[HALF:r[0-7]]], {{r[0-7]}}, 8
; WIDE-NEXT:   slli {{r[0-7]}}, [[HALF]], 3
  %result = shl i16 %value, 11
  ret i16 %result
}

define i16 @shift_right_twelve_size(i16 %value) minsize {
; SMALL-LABEL: shift_right_twelve_size:
; NANO:        ldi16 r0, __riscc_lshrhi12
; NANO-NEXT:   jalr r6, r0
; MIN:         ldi16 r0, __riscc_lshrhi12
; MIN-NEXT:    jalr s7, r0
; SYS:         jall s7, __riscc_lshrhi12
; WIDE-LABEL:  shift_right_twelve_size:
; WIDE:        srli [[HALF:r[0-7]]], {{r[0-7]}}, 8
; WIDE-NEXT:   srli {{r[0-7]}}, [[HALF]], 4
  %result = lshr i16 %value, 12
  ret i16 %result
}

define i16 @shift_arithmetic_fifteen_size(i16 %value) minsize {
; SMALL-LABEL: shift_arithmetic_fifteen_size:
; NANO:        ldi16 r0, __riscc_ashrhi15
; NANO-NEXT:   jalr r6, r0
; MIN:         ldi16 r0, __riscc_ashrhi15
; MIN-NEXT:    jalr s7, r0
; SYS:         jall s7, __riscc_ashrhi15
; WIDE-LABEL:  shift_arithmetic_fifteen_size:
; WIDE:        srai [[HALF:r[0-7]]], {{r[0-7]}}, 8
; WIDE-NEXT:   srai {{r[0-7]}}, [[HALF]], 7
  %result = ashr i16 %value, 15
  ret i16 %result
}

define i16 @shift_right_eight_size(i16 %value) minsize {
; SMALL-LABEL:   shift_right_eight_size:
; SMALL-NOT:     __riscc_lshrhi
; SMALL-COUNT-8: srli {{.*}}, 1
; SMALL:         {{ret s7|jalr r0, r6}}
; WIDE-LABEL:    shift_right_eight_size:
; WIDE:          srli {{.*}}, 8
  %result = lshr i16 %value, 8
  ret i16 %result
}

define i16 @shift_right_ten_size(i16 %value) minsize {
; SMALL-LABEL:    shift_right_ten_size:
; SMALL-NOT:      __riscc_lshrhi
; SMALL-COUNT-10: srli {{.*}}, 1
; SMALL:          {{ret s7|jalr r0, r6}}
; WIDE-LABEL:     shift_right_ten_size:
; WIDE:           srli [[HALF:r[0-7]]], {{r[0-7]}}, 8
; WIDE-NEXT:      srli {{r[0-7]}}, [[HALF]], 2
  %result = lshr i16 %value, 10
  ret i16 %result
}

define i16 @shift_right_fifteen_fast(i16 %value) {
; SMALL-LABEL:    shift_right_fifteen_fast:
; SMALL-NOT:      __riscc_lshrhi
; SMALL-COUNT-15: srli {{.*}}, 1
; SMALL:          {{ret s7|jalr r0, r6}}
; WIDE-LABEL:     shift_right_fifteen_fast:
; WIDE:           srli [[HALF:r[0-7]]], {{r[0-7]}}, 8
; WIDE-NEXT:      srli {{r[0-7]}}, [[HALF]], 7
  %result = lshr i16 %value, 15
  ret i16 %result
}

define i16 @shift_variables_size(i16 %value, i16 %amount) minsize {
; SMALL-LABEL: shift_variables_size:
; NANO-DAG:    ldi16 r0, __riscc_shlhi
; NANO-DAG:    ldi16 r0, __riscc_lshrhi
; NANO-DAG:    ldi16 r0, __riscc_ashrhi
; MIN-DAG:     ldi16 r0, __riscc_shlhi
; MIN-DAG:     ldi16 r0, __riscc_lshrhi
; MIN-DAG:     ldi16 r0, __riscc_ashrhi
; SYS-DAG:     jall s7, __riscc_shlhi
; SYS-DAG:     jall s7, __riscc_lshrhi
; SYS-DAG:     jall s7, __riscc_ashrhi
; WIDE-LABEL:  shift_variables_size:
; WIDE-NOT:    __riscc_{{shlhi|lshrhi|ashrhi}}
  %left = shl i16 %value, %amount
  %right = lshr i16 %value, %amount
  %arithmetic = ashr i16 %value, %amount
  %sum = add i16 %left, %right
  %result = add i16 %sum, %arithmetic
  ret i16 %result
}

define i16 @direct_call(i16 %value) {
; NANO-LABEL: direct_call:
; NANO:       ldi16 r0, callee
; NANO-NEXT:  jalr r6, r0
; NANO:       jalr r0, r{{[0-6]}}
; MIN-LABEL: direct_call:
; MIN:       ldi16 r0, callee
; MIN-NEXT:  jalr s7, r0
; SYS-LABEL: direct_call:
; SYS:       jall s7, callee
; FULL-LABEL: direct_call:
; FULL:       jall s7, callee
  %result = call i16 @callee(i16 %value)
  ret i16 %result
}
