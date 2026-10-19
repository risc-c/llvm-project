; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs -print-after=riscc-call-policy -o /dev/null %s 2>&1 | \
; RUN:   FileCheck %s --check-prefixes=IR,RC16
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs -print-after=riscc-call-policy -o /dev/null \
; RUN:   %s 2>&1 | FileCheck %s --check-prefixes=IR,RC32
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -enable-new-pm -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefix=ASM

; Private direct callers agree on unspecified high bits. The narrow add and
; store do not need an ABI-only normalization at either end of the call.
define internal signext i8 @narrow_add(i8 signext %a, i8 signext %b) optsize noinline {
; IR: define internal i8 @narrow_add(i8 %a, i8 %b)
; ASM-LABEL: narrow_add:
; ASM: add r1, r1, r2
; ASM-NOT: slli
; ASM-NOT: srai
; ASM-NOT: andi
; ASM: ret
  %sum = add i8 %a, %b
  ret i8 %sum
}
define void @store_sum(i16 %a, i16 %b, ptr %out) optsize {
  %x = trunc i16 %a to i8
  %y = trunc i16 %b to i8
  %r = call signext i8 @narrow_add(i8 signext %x, i8 signext %y)
; IR: %r = call i8 @narrow_add(i8 %x, i8 %y)
  store i8 %r, ptr %out
  ret void
}

; Only RC32 has a wider native word than i16. Match the unsigned consumer
; instead of sign-extending at return and masking at every call site.
define internal fastcc signext i16 @half_add(i16 signext %a) optsize noinline {
; RC16: define internal fastcc signext i16 @half_add(i16 signext %a)
; RC32: define internal fastcc zeroext i16 @half_add(i16 signext %a)
  %r = add i16 %a, 9
  ret i16 %r
}
define i32 @use_half(i16 %a) optsize {
  %r = call fastcc signext i16 @half_add(i16 signext %a)
; RC16: %r = call fastcc signext i16 @half_add(i16 signext %a)
; RC32: %r = call fastcc zeroext i16 @half_add(i16 signext %a)
  %wide = zext i16 %r to i32
  ret i32 %wide
}

; Semantic wide input uses retain their extension even if the result only
; reaches a narrow store. Return range information is retained too.
define internal signext range(i8 0, 128) i8 @wide_input(i8 signext %a) optsize {
; IR: define internal signext range(i8 0, -128) i8 @wide_input(i8 signext %a)
  %wide = sext i8 %a to i32
  %shift = ashr i32 %wide, 3
  %mask = and i32 %shift, 127
  %r = trunc i32 %mask to i8
  ret i8 %r
}
define void @store_wide_input(i8 %a, ptr %out) optsize {
  %r = call signext i8 @wide_input(i8 signext %a)
; IR: %r = call signext i8 @wide_input(i8 signext %a)
  store i8 %r, ptr %out
  ret void
}

; Public, address-taken, boolean, and speed-optimized interfaces are unchanged.
define signext i8 @public_add(i8 signext %a) optsize {
; IR: define signext i8 @public_add(i8 signext %a)
  %r = add i8 %a, 9
  ret i8 %r
}
@escaped = global ptr @address_taken
define internal zeroext i8 @address_taken(i8 zeroext %a) optsize {
; IR: define internal zeroext i8 @address_taken(i8 zeroext %a)
  ret i8 %a
}
define internal zeroext i1 @boolean(i1 zeroext %a) optsize {
; IR: define internal zeroext i1 @boolean(i1 zeroext %a)
  ret i1 %a
}
define internal signext i8 @mixed_callers(i8 signext %a) optsize {
; IR: define internal signext i8 @mixed_callers(i8 signext %a)
  ret i8 %a
}
define internal signext i8 @speed_callee(i8 signext %a) {
; IR: define internal signext i8 @speed_callee(i8 signext %a)
  ret i8 %a
}
define signext i8 @speed_caller(i8 signext %a) {
  %r = call signext i8 @mixed_callers(i8 signext %a)
; IR: %r = call signext i8 @mixed_callers(i8 signext %a)
  ret i8 %r
}
define i8 @size_caller(i8 %a, i1 %b) optsize {
  %bool = call zeroext i1 @boolean(i1 zeroext %b)
; IR: %bool = call zeroext i1 @boolean(i1 zeroext %b)
  %x = call signext i8 @mixed_callers(i8 signext %a)
; IR: %x = call signext i8 @mixed_callers(i8 signext %a)
  %y = call signext i8 @speed_callee(i8 signext %x)
; IR: %y = call signext i8 @speed_callee(i8 signext %x)
  %r = select i1 %bool, i8 %x, i8 %y
  ret i8 %r
}

; Both incoming and outgoing musttail contracts must retain matching ABI attrs.
define internal signext i8 @musttail_in(i8 signext %a) optsize {
; IR: define internal signext i8 @musttail_in(i8 signext %a)
  ret i8 %a
}
define signext i8 @tail_caller(i8 signext %a) optsize {
  %r = musttail call signext i8 @musttail_in(i8 signext %a)
; IR: %r = musttail call signext i8 @musttail_in(i8 signext %a)
  ret i8 %r
}
define internal signext i8 @musttail_out(i8 signext %a) optsize {
; IR: define internal signext i8 @musttail_out(i8 signext %a)
  %r = musttail call signext i8 @public_add(i8 signext %a)
  ret i8 %r
}
define signext i8 @call_out(i8 signext %a) optsize {
  %r = call signext i8 @musttail_out(i8 signext %a)
; IR: %r = call signext i8 @musttail_out(i8 signext %a)
  ret i8 %r
}
