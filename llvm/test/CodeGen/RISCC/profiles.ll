; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -verify-machineinstrs < %s | FileCheck %s --check-prefixes=NANO,SMALL
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -verify-machineinstrs < %s | FileCheck %s --check-prefixes=MIN,SMALL
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -verify-machineinstrs < %s | FileCheck %s --check-prefixes=SYS,WIDE
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefixes=FULL,WIDE

target triple = "riscc-none-elf"

declare i16 @callee(i16)

define i16 @multiply(i16 %a, i16 %b) {
; NANO-LABEL: multiply:
; NANO:       li r0, code(__mulhi3)
; NANO-NEXT:  jal r6, r0
; NANO:       ret r{{[0-6]}}
; MIN-LABEL: multiply:
; MIN:       li r0, code(__mulhi3)
; MIN-NEXT:  jal s7, r0
; SYS-LABEL: multiply:
; SYS:       call16 code(__mulhi3)
; FULL-LABEL: multiply:
; FULL:       mul
  %result = mul i16 %a, %b
  ret i16 %result
}

define i16 @shift_left_five(i16 %value) {
; NANO-LABEL: shift_left_five:
; NANO:       add
; NANO-NOT:   shli
; NANO:       ret r6
; MIN-LABEL: shift_left_five:
; MIN:       add
; MIN-NOT:   shli
; SYS-LABEL: shift_left_five:
; SYS:       shli
; FULL-LABEL: shift_left_five:
; FULL:       shli
  %result = shl i16 %value, 5
  ret i16 %result
}

define i16 @shift_right_five(i16 %value) {
; NANO-LABEL: shift_right_five:
; NANO:       shri {{.*}}, 1
; NANO-NOT:   shri {{.*}}, 5
; NANO:       ret r6
; MIN-LABEL: shift_right_five:
; MIN:       shri {{.*}}, 1
; MIN-NOT:   shri {{.*}}, 5
; SYS-LABEL: shift_right_five:
; SYS:       shri {{.*}}, 5
; FULL-LABEL: shift_right_five:
; FULL:       shri {{.*}}, 5
  %result = lshr i16 %value, 5
  ret i16 %result
}

define i16 @shift_left_eleven_size(i16 %value) minsize {
; SMALL-LABEL: shift_left_eleven_size:
; SMALL:       li r0, code(__riscc_shlhi11)
; NANO-NEXT:   jal r6, r0
; MIN-NEXT:    jal s7, r0
; WIDE-LABEL:  shift_left_eleven_size:
; WIDE:        shli [[HALF:r[0-7]]], {{r[0-7]}}, 8
; WIDE-NEXT:   shli {{r[0-7]}}, [[HALF]], 3
  %result = shl i16 %value, 11
  ret i16 %result
}

define i16 @shift_right_twelve_size(i16 %value) minsize {
; SMALL-LABEL: shift_right_twelve_size:
; SMALL:       li r0, code(__riscc_lshrhi12)
; NANO-NEXT:   jal r6, r0
; MIN-NEXT:    jal s7, r0
; WIDE-LABEL:  shift_right_twelve_size:
; WIDE:        shri [[HALF:r[0-7]]], {{r[0-7]}}, 8
; WIDE-NEXT:   shri {{r[0-7]}}, [[HALF]], 4
  %result = lshr i16 %value, 12
  ret i16 %result
}

define i16 @shift_arithmetic_fifteen_size(i16 %value) minsize {
; SMALL-LABEL: shift_arithmetic_fifteen_size:
; SMALL:       li r0, code(__riscc_ashrhi15)
; NANO-NEXT:   jal r6, r0
; MIN-NEXT:    jal s7, r0
; WIDE-LABEL:  shift_arithmetic_fifteen_size:
; WIDE:        sari [[HALF:r[0-7]]], {{r[0-7]}}, 8
; WIDE-NEXT:   sari {{r[0-7]}}, [[HALF]], 7
  %result = ashr i16 %value, 15
  ret i16 %result
}

define i16 @shift_right_eight_size(i16 %value) minsize {
; SMALL-LABEL:   shift_right_eight_size:
; SMALL-NOT:     __riscc_lshrhi
; SMALL-COUNT-8: shri {{.*}}, 1
; SMALL:         ret
; WIDE-LABEL:    shift_right_eight_size:
; WIDE:          shri {{.*}}, 8
  %result = lshr i16 %value, 8
  ret i16 %result
}

define i16 @shift_right_ten_size(i16 %value) minsize {
; SMALL-LABEL:    shift_right_ten_size:
; SMALL-NOT:      __riscc_lshrhi
; SMALL-COUNT-10: shri {{.*}}, 1
; SMALL:          ret
; WIDE-LABEL:     shift_right_ten_size:
; WIDE:           shri [[HALF:r[0-7]]], {{r[0-7]}}, 8
; WIDE-NEXT:      shri {{r[0-7]}}, [[HALF]], 2
  %result = lshr i16 %value, 10
  ret i16 %result
}

define i16 @shift_right_fifteen_fast(i16 %value) {
; SMALL-LABEL:    shift_right_fifteen_fast:
; SMALL-NOT:      __riscc_lshrhi
; SMALL-COUNT-15: shri {{.*}}, 1
; SMALL:          ret
; WIDE-LABEL:     shift_right_fifteen_fast:
; WIDE:           shri [[HALF:r[0-7]]], {{r[0-7]}}, 8
; WIDE-NEXT:      shri {{r[0-7]}}, [[HALF]], 7
  %result = lshr i16 %value, 15
  ret i16 %result
}

define i16 @direct_call(i16 %value) {
; NANO-LABEL: direct_call:
; NANO:       li r0, code(callee)
; NANO-NEXT:  jal r6, r0
; NANO:       ret r{{[0-6]}}
; MIN-LABEL: direct_call:
; MIN:       li r0, code(callee)
; MIN-NEXT:  jal s7, r0
; SYS-LABEL: direct_call:
; SYS:       call16 code(callee)
; FULL-LABEL: direct_call:
; FULL:       call16 code(callee)
  %result = call i16 @callee(i16 %value)
  ret i16 %result
}
