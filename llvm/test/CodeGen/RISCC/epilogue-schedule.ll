; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -enable-new-pm -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -O0 -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefix=UNSCHEDULED

declare void @use(ptr)

; Return through the reloaded GPR after the independent SP adjustment.
define i32 @small_frame() {
; CHECK-LABEL: small_frame:
; CHECK:       ld r0, [r7 + 0]
; CHECK-NEXT:  addi r7, 20
; CHECK-NEXT:  jalr s0, r0
; UNSCHEDULED-LABEL: small_frame:
; UNSCHEDULED:       ld r0, [r7 + 0]
; UNSCHEDULED-NEXT:  addi r7, 20
; UNSCHEDULED-NEXT:  mts s7, r0
  %buf = alloca [4 x i32], align 4
  call void @use(ptr %buf)
  %x = load i32, ptr %buf
  ret i32 %x
}

; A large adjustment initially uses r0 as scratch. Scheduling renames that
; scratch, so the late cleanup can also remove this return's S-register copy.
; At O0, without renaming, the link must still be preserved in S7.
define i32 @large_frame() {
; CHECK-LABEL: large_frame:
; CHECK:       ld r0, [r7 + 4]
; CHECK-NEXT:  ldpc [[SIZE:r[1-6]]],
; CHECK-NEXT:  add r7, r7, [[SIZE]]
; CHECK-NEXT:  jalr s0, r0
; UNSCHEDULED-LABEL: large_frame:
; UNSCHEDULED:       ld r0, [r7 + 4]
; UNSCHEDULED-NEXT:  mts s7, r0
; UNSCHEDULED-NEXT:  ldpc r0,
; UNSCHEDULED-NEXT:  add r7, r7, r0
  %buf = alloca [256 x i32], align 4
  call void @use(ptr %buf)
  %x = load i32, ptr %buf
  ret i32 %x
}
