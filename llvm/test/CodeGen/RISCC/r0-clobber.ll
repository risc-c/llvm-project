; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

; r0 is an ordinary caller-saved allocation candidate.  This function has no
; comparisons, so an r0 occurrence demonstrates ordinary register allocation
; rather than the compare instruction's fixed physical definition.
define i16 @allocate_r0(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: allocate_r0:
; CHECK:       {{(add|xor|or|mul)}}	r0,
; CHECK:       ret s7
  %ab = add i16 %a, %b
  %cd = xor i16 %c, %d
  %ac = mul i16 %a, %c
  %bd = or i16 %b, %d
  %x = mul i16 %ab, %cd
  %y = mul i16 %ac, %bd
  %v = add i16 %x, %y
  ret i16 %v
}

; The branch must consume SUB's r0 result before another instruction clobbers it.
define i16 @compare_branch(i16 %a, i16 %b, i16 %x) {
; CHECK-LABEL: compare_branch:
; CHECK:       sub	r0, r1, r2
; CHECK-NEXT:  bnez
  %same = icmp eq i16 %a, %b
  br i1 %same, label %equal, label %different

equal:
  %inc = add i16 %x, 1
  ret i16 %inc

different:
  %dec = sub i16 %x, 1
  ret i16 %dec
}
