; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s

declare i32 @callee(i32)

define i32 @indirect_tail(ptr %f, i32 %x) {
; CHECK-LABEL: indirect_tail:
; CHECK: mov r3, r1
; CHECK: mov r1, r2
; CHECK: jalr s0, r3
; CHECK-NOT: ret
  %r = tail call i32 %f(i32 %x)
  ret i32 %r
}

define i32 @mandatory_indirect_tail(ptr %f, i32 %x) {
; CHECK-LABEL: mandatory_indirect_tail:
; CHECK: mov r3, r1
; CHECK: jalr s0, r3
; CHECK-NOT: ret
  %r = musttail call i32 %f(ptr %f, i32 %x)
  ret i32 %r
}

define i32 @call_then_indirect(ptr %f, i32 %x) {
; CHECK-LABEL: call_then_indirect:
; CHECK: jalr s7,
; CHECK: mts s7,
; CHECK: jalr s0, r3
; CHECK-NOT: ret
  %a = call i32 @callee(i32 %x)
  %r = tail call i32 %f(i32 %a)
  ret i32 %r
}

define i32 @large_indirect_frame(ptr %f, i32 %x) {
; CHECK-LABEL: large_indirect_frame:
; CHECK: sub r7, r7,
; CHECK: add r7, r7,
; CHECK: jalr s0, r3
; CHECK-NOT: ret
  %frame = alloca [300 x i32], align 4
  %slot = getelementptr [300 x i32], ptr %frame, i32 0, i32 299
  store volatile i32 %x, ptr %slot
  %a = load volatile i32, ptr %slot
  %r = tail call i32 %f(i32 %a)
  ret i32 %r
}
