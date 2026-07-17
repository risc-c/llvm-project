; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -verify-machineinstrs < %s | FileCheck %s --check-prefix=MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -verify-machineinstrs < %s | FileCheck %s --check-prefix=SYS
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s --check-prefix=FULL

target triple = "riscc-none-elf"

declare i16 @callee(i16)

define i16 @multiply(i16 %a, i16 %b) {
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

define i16 @direct_call(i16 %value) {
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
