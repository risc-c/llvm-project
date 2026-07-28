; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 < %s | FileCheck %s --check-prefix=BASE
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+mdu -O2 < %s | FileCheck %s --check-prefix=MDU

define i16 @quotient(i16 %a, i16 %b) {
; BASE-LABEL: quotient:
; BASE: call16 code(__udivhi3)
; MDU-LABEL: quotient:
; MDU: ldi r0, 0
; MDU-NEXT: divu r0, r1, r2
; MDU-NEXT: rets
  %q = udiv i16 %a, %b
  ret i16 %q
}

define i16 @remainder(i16 %a, i16 %b) {
; BASE-LABEL: remainder:
; BASE: call16 code(__umodhi3)
; MDU-LABEL: remainder:
; MDU: ldi r0, 0
; MDU-NEXT: divu r0, r1, r2
; MDU-NEXT: mov r1, r0
; MDU-NEXT: rets
  %r = urem i16 %a, %b
  ret i16 %r
}

define i16 @both(i16 %a, i16 %b) {
; MDU-LABEL: both:
; MDU: ldi r0, 0
; MDU-NEXT: divu r0, r1, r2
; MDU-NEXT: add r1, r1, r0
; MDU-NEXT: rets
  %q = udiv i16 %a, %b
  %r = urem i16 %a, %b
  %sum = add i16 %q, %r
  ret i16 %sum
}
