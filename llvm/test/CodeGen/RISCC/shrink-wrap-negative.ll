; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -riscc-shrink-wrap=true -verify-machineinstrs < %s | FileCheck %s \
; RUN:   --check-prefix=UW

target datalayout = "e-p:16:16"
target triple = "riscc-none-elf"

declare i16 @uwtable_callee(i16)

; The target does not yet describe moved frame state to the unwinder. Leave
; the prologue at entry even though the cold call would otherwise make this
; a useful shrink-wrap case.
define i16 @uwtable_hot_cold(i16 %x, i1 %take) nounwind uwtable {
; UW-LABEL: uwtable_hot_cold:
; UW:       addi r7, -2
; UW:       mfs r0, s7
; UW:       st r0, [r7 + 0]
; UW-NOT:   ret s7
; UW:       beqz
; UW:       ld r0, [r7 + 0]
; UW:       addi r7, 2
; UW:       jalr s0, r0
  br i1 %take, label %fast, label %cold, !prof !0
fast:
  ret i16 %x
cold:
  %called = call i16 @uwtable_callee(i16 %x)
  %adjusted = add i16 %called, 1
  ret i16 %adjusted
}

!0 = !{!"branch_weights", i32 1000, i32 1}
