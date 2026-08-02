; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -O0 -verify-machineinstrs < %s | FileCheck %s

target triple = "riscc-none-elf"

declare i16 @callee(i16)

define i16 @non_leaf(i16 %value) {
; CHECK-LABEL: non_leaf:
; CHECK:       stw r6,
; CHECK:       li r0, code(callee)
; CHECK-NEXT:  jalr r6, r0
; CHECK:       ldw r6,
; CHECK:       ret r6
  %result = call i16 @callee(i16 %value)
  %sum = add i16 %result, %value
  ret i16 %sum
}

define i16 @large_frame(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: large_frame:
; CHECK:       li [[SIZE:r[0-6]]], 3{{[0-9][0-9]}}
; CHECK:       sub r7, r7, [[SIZE]]
; CHECK:       stw r6,
; CHECK:       li r0, code(callee)
; CHECK-NEXT:  jalr r6, r0
; CHECK:       ldw r6,
; CHECK:       li [[RESTORE:r[0-6]]], 3{{[0-9][0-9]}}
; CHECK:       add r7, r7, [[RESTORE]]
; CHECK:       ret r6
  %buffer = alloca [300 x i8], align 2
  %slot = getelementptr inbounds [300 x i8], ptr %buffer, i16 0, i16 299
  store volatile i8 7, ptr %slot, align 1
  %result = call i16 @callee(i16 %a)
  %saved = load volatile i8, ptr %slot, align 1
  %extended = zext i8 %saved to i16
  %sum0 = add i16 %result, %extended
  %sum1 = add i16 %sum0, %b
  %sum2 = add i16 %sum1, %c
  %sum3 = add i16 %sum2, %d
  ret i16 %sum3
}
