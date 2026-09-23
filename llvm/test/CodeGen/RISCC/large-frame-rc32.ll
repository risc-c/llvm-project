; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s

; Large frames and far stack accesses must not emit dozens of immediate adds.
; CHECK-LABEL: frame:
; CHECK: ldpc r0,
; CHECK-NEXT: sub r7, r7, r0
; CHECK: ldpc [[ADDR:r[0-6]]],
; CHECK: add [[ADDR]], {{r[0-6]}}, [[ADDR]]
; CHECK: st r1,
; CHECK: ldpc r0,
; CHECK-NEXT: add r7, r7, r0
; CHECK: ret

define i32 @frame(i32 %value) {
  %a = alloca [1024 x i32], align 4
  %end = getelementptr [1024 x i32], ptr %a, i32 0, i32 1023
  store volatile i32 %value, ptr %a, align 4
  store volatile i32 %value, ptr %end, align 4
  %v = load volatile i32, ptr %end, align 4
  ret i32 %v
}
