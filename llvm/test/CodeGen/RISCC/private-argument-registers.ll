; REQUIRES: riscc-registered-target
; RUN: split-file %s %t
; RUN: llc -mtriple=riscc -mcpu=full -riscc-private-arg-registers=6 -stop-after=riscc-call-policy %t/eligible.ll -o - \
; RUN:   | FileCheck %s --check-prefixes=POLICY,RC16
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -stop-after=riscc-call-policy %t/eligible.ll -o - | FileCheck %s \
; RUN:   --check-prefixes=POLICY,RC32
; RUN: llc -enable-new-pm -mtriple=riscc -mcpu=full -mattr=+rc32 -riscc-private-arg-registers=6 \
; RUN:   -stop-after=riscc-call-policy %t/eligible.ll -o - | FileCheck %s --check-prefixes=POLICY,RC32
; RUN: llc -mtriple=riscc -mcpu=full -riscc-private-arg-registers=3 -stop-after=riscc-call-policy %t/eligible.ll -o - \
; RUN:   | FileCheck %s --check-prefix=OFF --implicit-check-not=riscc-private-reg-args
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %t/eligible.ll -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -riscc-private-arg-registers=6 -verify-machineinstrs %t/eligible.ll \
; RUN:   -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -enable-new-pm -mtriple=riscc -mcpu=full -mattr=+rc32 -riscc-private-arg-registers=6 -verify-machineinstrs \
; RUN:   %t/eligible.ll -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -riscc-private-arg-registers=6 -verify-machineinstrs %t/eligible.ll \
; RUN:   -o %t/min.s
; RUN: llc -mtriple=riscc -mcpu=nano -riscc-private-arg-registers=6 -verify-machineinstrs %t/eligible.ll -o - | \
; RUN:   FileCheck %s --check-prefix=NANO
; RUN: llc -mtriple=riscc -mcpu=full -riscc-private-arg-registers=6 -stop-after=finalize-isel -verify-machineinstrs \
; RUN:   %t/eligible.ll -o - | FileCheck %s --check-prefix=MASK
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -riscc-private-arg-registers=6 -stop-after=finalize-isel \
; RUN:   -verify-machineinstrs %t/eligible.ll -o - | FileCheck %s --check-prefix=MASK
; RUN: llc -mtriple=riscc -mcpu=full -riscc-private-arg-registers=6 -stop-after=riscc-call-policy %t/ineligible.ll \
; RUN:   -o - | FileCheck %s --check-prefix=GUARD --implicit-check-not=riscc-private-reg-args
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -riscc-private-arg-registers=6 -stop-after=riscc-call-policy \
; RUN:   %t/ineligible.ll -o - | FileCheck %s --check-prefix=GUARD --implicit-check-not=riscc-private-reg-args

; Only a closed, size-optimized direct interface gets additional registers.
; Both widths use native slots; RC16's wide scalars consume two each. Never
; split a wide argument between registers and the stack. Return values keep
; the public convention, as do the externally visible entry points below.
; POLICY: define internal i16 @four({{.*}}) #[[FOUR:[0-9]+]]
; POLICY: define internal ptr @six({{.*}}) #[[SIX:[0-9]+]]
; RC16: define internal i32 @wide({{.*}}) #[[SIX]]
; RC32: define internal i32 @wide({{.*}}) #[[FOUR]]
; POLICY: define internal fastcc i32 @straddle({{.*}}) #[[FIVE:[0-9]+]]
; POLICY: attributes #[[FOUR]] = { noinline optsize {{.*}}"riscc-private-reg-args"="4"
; POLICY: attributes #[[SIX]] = { noinline optsize {{.*}}"riscc-private-reg-args"="6"
; POLICY: attributes #[[FIVE]] = { noinline optsize {{.*}}"riscc-private-reg-args"="5"
; OFF: define internal i16 @four(

; The fourth argument is used directly, without saving a supposedly preserved
; R4 or loading it from the stack. The same holds for R4..R6 in the six-arg leaf.
; ASM-LABEL: four:
; ASM-NOT: mts
; ASM-NOT: ld
; ASM: mul r{{[0-6]}}, r3, r4
; ASM-NOT: mfs
; ASM: ret s3
; ASM-LABEL: six:
; ASM-NOT: mts
; ASM-NOT: ld
; ASM: st r4, [r3 + 0]
; ASM: st r6, [r5 + 0]
; ASM-NOT: mfs
; ASM: ret s3

; A public caller cannot tail-jump with R4..R6 arguments: its epilogue could
; restore their incoming public-ABI values over the newly prepared arguments.
; ASM-LABEL: call_six:
; ASM: {{jall|jalr}} s3,
; ASM: ret s7

; Nano retains R6 for its link; the sixth argument remains on the stack.
; NANO-LABEL: six:
; NANO: ld r{{[0-5]}}, [r7 + 0]
; NANO-NOT: st r6,
; NANO: jalr r0, r6

; Each private call clobbers exactly its additional argument registers. A
; public call made from a six-register private function still has the public
; callee's mask, not that of its caller.
; MASK-LABEL: name:            call_four
; MASK: @four, CustomRegMask($r5,$r6,$r7,$s5,$s6)
; MASK-LABEL: name:            call_six
; MASK: @six, CustomRegMask($r7,$s5,$s6)
; MASK-LABEL: name:            nested_six
; MASK: @opaque, csr_riscc_callpreserved,
; MASK-LABEL: name:            call_nested
; MASK: @nested_six, CustomRegMask($r7,$s5,$s6)

; Stale annotations must be removed, not trusted. Stop after the policy here:
; several protected IR constructs (byval, stack-argument musttail) deliberately
; exceed the backend's current general lowering support.
; GUARD: define i16 @public_four(
; GUARD: define internal i16 @escaped(
; GUARD: define internal i16 @mixed(
; GUARD: define internal i16 @speed(
; GUARD: define internal i16 @varargs(
; GUARD: define internal i16 @byval(
; GUARD: define internal void @sret(
; GUARD: define internal i16 @nest(
; GUARD: define internal i16 @musttail_in(
; GUARD: define internal i16 @musttail_out(
; GUARD: define internal void @naked_entry(
; GUARD: define internal i16 @interrupt_entry(
; GUARD: define internal i16 @float_arg(
; GUARD: define internal i16 @unused(

;--- eligible.ll
define internal i16 @four(i16 %a, i16 %b, i16 %c, i16 %d) #0 {
  %x = add i16 %a, %b
  %y = mul i16 %c, %d
  %r = xor i16 %x, %y
  ret i16 %r
}
define internal ptr @six(ptr %a, ptr %b, ptr %c, ptr %d, ptr %e, ptr %f) #0 {
  store ptr %b, ptr %a
  store ptr %d, ptr %c
  store ptr %f, ptr %e
  ret ptr %e
}
define internal i32 @wide(i16 %a, i16 %b, i32 %c, i32 %d) #0 {
  %x = zext i16 %a to i32
  %y = zext i16 %b to i32
  %s = add i32 %x, %c
  %t = add i32 %y, %d
  %r = xor i32 %s, %t
  ret i32 %r
}
define internal fastcc i32 @straddle(ptr %a, ptr %b, ptr %c, i32 %d, i32 %e) #0 {
  store ptr %b, ptr %a
  store i32 %d, ptr %c
  ret i32 %e
}
define i16 @call_four(i16 %a, i16 %b, i16 %c, i16 %d) optsize {
  %x = call i16 @four(i16 %a, i16 %b, i16 %c, i16 %d)
  %r = add i16 %x, %d
  ret i16 %r
}
define ptr @call_six(ptr %a, ptr %b, ptr %c, ptr %d, ptr %e, ptr %f) optsize {
  %r = tail call ptr @six(ptr %a, ptr %b, ptr %c, ptr %d, ptr %e, ptr %f)
  ret ptr %r
}
define i32 @call_wide(i16 %a, i16 %b, i32 %c, i32 %d) optsize {
  %r = call i32 @wide(i16 %a, i16 %b, i32 %c, i32 %d)
  ret i32 %r
}
define i32 @call_straddle(ptr %a, ptr %b, ptr %c, i32 %d, i32 %e) optsize {
  %r = call fastcc i32 @straddle(ptr %a, ptr %b, ptr %c, i32 %d, i32 %e)
  ret i32 %r
}
declare ptr @opaque(ptr)
define internal ptr @nested_six(ptr %a, ptr %b, ptr %c, ptr %d, ptr %e, ptr %f) #0 {
  %r = call ptr @opaque(ptr %a)
  store ptr %b, ptr %c
  store ptr %d, ptr %e
  store ptr %r, ptr %f
  ret ptr %r
}
define ptr @call_nested(ptr %a, ptr %b, ptr %c, ptr %d, ptr %e, ptr %f) optsize {
  %r = call ptr @nested_six(ptr %a, ptr %b, ptr %c, ptr %d, ptr %e, ptr %f)
  ret ptr %r
}
attributes #0 = { noinline optsize "riscc-private-reg-args"="6" }

;--- ineligible.ll
@address = global ptr @escaped
declare i16 @external(i16, i16, i16, i16)
define i16 @public_four(i16 %a, i16 %b, i16 %c, i16 %d) #0 { ret i16 %d }
define internal i16 @escaped(i16 %a, i16 %b, i16 %c, i16 %d) #0 { ret i16 %d }
define internal i16 @mixed(i16 %a, i16 %b, i16 %c, i16 %d) #0 { ret i16 %d }
define internal i16 @speed(i16 %a, i16 %b, i16 %c, i16 %d) #1 { ret i16 %d }
define internal i16 @varargs(i16 %a, i16 %b, i16 %c, i16 %d, ...) #0 { ret i16 %d }
define internal i16 @byval(ptr byval(i16) %a, i16 %b, i16 %c, i16 %d) #0 { ret i16 %d }
define internal void @sret(ptr sret(i16) %a, i16 %b, i16 %c, i16 %d) #0 { ret void }
define internal i16 @nest(ptr nest %a, i16 %b, i16 %c, i16 %d) #0 { ret i16 %d }
define internal i16 @musttail_in(i16 %a, i16 %b, i16 %c, i16 %d) #0 { ret i16 %d }
define internal i16 @musttail_out(i16 %a, i16 %b, i16 %c, i16 %d) #0 {
  %r = musttail call i16 @external(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %r
}
define internal void @naked_entry(i16 %a, i16 %b, i16 %c, i16 %d) #0 naked { unreachable }
define internal i16 @interrupt_entry(i16 %a, i16 %b, i16 %c, i16 %d) #0 "interrupt" { ret i16 %d }
define internal i16 @float_arg(float %a, i16 %b, i16 %c, i16 %d, i16 %e) #0 { ret i16 %d }
define internal i16 @unused(i16 %a, i16 %b, i16 %c, i16 %d) #0 { ret i16 %d }
define i16 @size_user(i16 %a, ptr %p) optsize {
  %r0 = call i16 @public_four(i16 %a, i16 %a, i16 %a, i16 %a)
  %r1 = call i16 @escaped(i16 %a, i16 %a, i16 %a, i16 %a)
  %r2 = call i16 @mixed(i16 %a, i16 %a, i16 %a, i16 %a)
  %r3 = call i16 @speed(i16 %a, i16 %a, i16 %a, i16 %a)
  %r4 = call i16 (i16, i16, i16, i16, ...) @varargs(i16 %a, i16 %a, i16 %a, i16 %a)
  %r5 = call i16 @byval(ptr byval(i16) %p, i16 %a, i16 %a, i16 %a)
  call void @sret(ptr sret(i16) %p, i16 %a, i16 %a, i16 %a)
  %r6 = call i16 @nest(ptr nest %p, i16 %a, i16 %a, i16 %a)
  %r7 = call i16 @musttail_out(i16 %a, i16 %a, i16 %a, i16 %a)
  call void @naked_entry(i16 %a, i16 %a, i16 %a, i16 %a)
  %r8 = call i16 @interrupt_entry(i16 %a, i16 %a, i16 %a, i16 %a)
  %r9 = call i16 @float_arg(float 1.0, i16 %a, i16 %a, i16 %a, i16 %a)
  ret i16 %r9
}
define i16 @speed_user(i16 %a) {
  %r = call i16 @mixed(i16 %a, i16 %a, i16 %a, i16 %a)
  ret i16 %r
}
define i16 @tail_user(i16 %a, i16 %b, i16 %c, i16 %d) optsize {
  %r = musttail call i16 @musttail_in(i16 %a, i16 %b, i16 %c, i16 %d)
  ret i16 %r
}
attributes #0 = { noinline optsize "riscc-private-reg-args"="6" }
attributes #1 = { noinline "riscc-private-reg-args"="6" }
