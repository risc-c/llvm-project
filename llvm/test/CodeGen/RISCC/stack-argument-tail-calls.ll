; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefixes=CHECK,RC16
; RUN: sed -e 's/i16/i32/g' -e 's/align 2/align 4/g' %s | llc -mtriple=riscc -mcpu=full -mattr=+rc32 \
; RUN:   -verify-machineinstrs -o - | FileCheck %s --check-prefixes=CHECK,RC32
; RUN: sed 's/musttail call/tail call/' %s | llc -mtriple=riscc -mcpu=full -riscc-stack-argument-tail-calls=false \
; RUN:   -verify-machineinstrs -o - | FileCheck %s --check-prefix=OFF
; RUN: llc -enable-new-pm -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=riscc -mcpu=min -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=riscc -mcpu=nano -verify-machineinstrs %s -o /dev/null
; RUN: sed -e 's/i16/i32/g' -e 's/align 2/align 4/g' %s | llc -enable-new-pm -mtriple=riscc -mcpu=min -mattr=+rc32 \
; RUN:   -verify-machineinstrs -o /dev/null

declare i16 @four(i16, i16, i16, i16)
declare i16 @five(i16, i16, i16, i16, i16)
declare i16 @narrow(i16, i16, i16, i8 zeroext)
declare i16 @first(i16)

; The whole incoming slot matches, not merely its offset or low bits.
define i16 @forward(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: forward:
; CHECK-NOT: addi r7,
; CHECK-NOT: mfs
; CHECK-NOT: mts
; RC16: jall s0, four
; RC32: jalr s0, r0
; CHECK-NOT: ret
; CHECK: .Lfunc_end
; OFF-LABEL: forward:
; OFF: jall s7, four
  %r = tail call i16 @four(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %r
}

; Recognize the incoming load even when the entry DAG is already selected.
define i16 @forward_from_block(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: forward_from_block:
; CHECK-NOT: addi r7,
; RC16: jall s0, four
; RC32: jalr s0, r0
  %go = icmp ne i16 %a, 0
  br i1 %go, label %tail, label %done
tail:
  %r = tail call i16 @four(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %r
done:
  ret i16 %d
}

; Local storage must be removed before the callee uses the original SP.
define i16 @local_frame(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: local_frame:
; CHECK: addi r7, -{{[24]}}
; CHECK: st r1,
; CHECK: ld r1,
; CHECK: addi r7, {{[24]}}
; RC16: jall s0, four
; RC32: jalr s0, r0
  %slot = alloca i16, align 2
  store volatile i16 %a, ptr %slot, align 2
  %v = load volatile i16, ptr %slot, align 2
  %r = tail call i16 @four(i16 %v, i16 %b, i16 %c, i16 %d)
  ret i16 %r
}

; Intervening calls require the normal return-link restoration, but do not
; overwrite immutable incoming argument slots above our frame.
define i16 @after_call(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: after_call:
; RC16: jall s7, first
; RC32: jalr s7,
; CHECK: mts s7,
; RC16: jall s0, four
; RC32: jalr s0, r0
  %v = call i16 @first(i16 %a)
  %r = tail call i16 @four(i16 %v, i16 %b, i16 %c, i16 %d)
  ret i16 %r
}

define i16 @changed(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: changed:
; RC16: jall s7, four
; RC32: jalr s7,
  %v = add i16 %d, 1
  %r = tail call i16 @four(i16 %a, i16 %b, i16 %c, i16 %v)
  ret i16 %r
}

define i16 @mandatory(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: mandatory:
; CHECK-NOT: addi r7,
; RC16: jall s0, four
; RC32: jalr s0, r0
  %r = musttail call i16 @four(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %r
}

define i16 @swapped(i16 %a, i16 %b, i16 %c, i16 %d, i16 %e) {
; CHECK-LABEL: swapped:
; RC16: jall s7, five
; RC32: jalr s7,
  %r = tail call i16 @five(i16 %a, i16 %b, i16 %c, i16 %e, i16 %d)
  ret i16 %r
}

define i16 @different_extension(i16 %a, i16 %b, i16 %c, i8 signext %d) {
; CHECK-LABEL: different_extension:
; RC16: jall s7, narrow
; RC32: jalr s7,
  %r = tail call i16 @narrow(i16 %a, i16 %b, i16 %c, i8 zeroext %d)
  ret i16 %r
}

define i16 @local_slot(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: local_slot:
; RC16: jall s7, four
; RC32: jalr s7,
  %slot = alloca i16, align 2
  store volatile i16 %d, ptr %slot, align 2
  %v = load volatile i16, ptr %slot, align 2
  %r = tail call i16 @four(i16 %a, i16 %b, i16 %c, i16 %v)
  ret i16 %r
}
