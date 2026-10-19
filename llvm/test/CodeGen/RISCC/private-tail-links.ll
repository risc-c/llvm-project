; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=ASM,RC16
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s \
; RUN:   --check-prefixes=ASM,RC32
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s \
; RUN:   --check-prefixes=ASM,RC32
; RUN: llc -enable-new-pm -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | \
; RUN:   FileCheck %s --check-prefixes=ASM,RC32
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O0 -print-after=riscc-call-policy -o /dev/null < %s 2>&1 | \
; RUN:   FileCheck %s --check-prefix=POLICY
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -print-after=riscc-call-policy -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=POLICY

target triple = "riscc-none-elf"

; No ordinary call needs a distinct private link here. Inherit S7 all the
; way down the chain instead of copying it to S3 at the public tail entry.
define internal i16 @tail_leaf(i16 %x) {
; ASM-LABEL: tail_leaf:
; ASM: ret s7
; POLICY: define internal i16 @tail_leaf(i16 %x) #[[PUBLIC:[0-9]+]]
  %r = add i16 %x, 1
  ret i16 %r
}

define internal i16 @tail_chain(i16 %x) {
; ASM-LABEL: tail_chain:
; ASM-NOT: mfs
; ASM-NOT: mts
; RC16: jall s0, tail_leaf
; RC32: jalr s0, r0
; POLICY: define internal i16 @tail_chain(i16 %x) #[[PUBLIC]]
  %r = tail call i16 @tail_leaf(i16 %x)
  ret i16 %r
}

define i16 @public_tail(i16 %x) {
; ASM-LABEL: public_tail:
; ASM-NOT: mfs
; ASM-NOT: mts
; RC16: jall s0, tail_chain
; RC32: jalr s0, r0
  %r = musttail call i16 @tail_chain(i16 %x)
  ret i16 %r
}

; A normal private call still selects S3. Its tail-only descendants inherit
; S3, not S7, even if they also have public tail-call users.
define internal i16 @private_leaf(i16 %x) {
; POLICY: define internal i16 @private_leaf(i16 %x) #[[PRIVATE:[0-9]+]]
  ret i16 %x
}
define internal i16 @private_root(i16 %x) {
; POLICY: define internal i16 @private_root(i16 %x) #[[PRIVATE]]
  %r = tail call i16 @private_leaf(i16 %x)
  ret i16 %r
}
define i16 @ordinary_call(i16 %x) {
  %r = call i16 @private_root(i16 %x)
  ret i16 %r
}
define i16 @mixed_tail(i16 %x) {
  %r = tail call i16 @private_leaf(i16 %x)
  ret i16 %r
}

; Tail-only recursion is a fixed point, independent of function order.
define internal i16 @cycle_a(i16 %x) {
; POLICY: define internal i16 @cycle_a(i16 %x) #[[PUBLIC]]
  %r = tail call i16 @cycle_b(i16 %x)
  ret i16 %r
}
define internal i16 @cycle_b(i16 %x) {
; POLICY: define internal i16 @cycle_b(i16 %x) #[[PUBLIC]]
  %r = tail call i16 @cycle_a(i16 %x)
  ret i16 %r
}
define i16 @enter_cycle(i16 %x) {
  %r = tail call i16 @cycle_b(i16 %x)
  ret i16 %r
}

; A tail hint with stack arguments is not a sibling transfer. Retain the
; ordinary private-call convention; do not classify it from the hint alone.
define internal i16 @stack_target(i16 %a, i16 %b, i16 %c, i16 %d) {
; POLICY: define internal i16 @stack_target(i16 %a, i16 %b, i16 %c, i16 %d) #[[PRIVATE]]
  ret i16 %d
}
define i16 @stack_hint(i16 %x) {
  %r = tail call i16 @stack_target(i16 %x, i16 %x, i16 %x, i16 %x)
  ret i16 %r
}

; Address-taken local entries always retain the public convention.
@escaped = global ptr @address_taken
define internal i16 @address_taken(i16 %x) {
; POLICY: define internal i16 @address_taken(i16 %x) #[[PUBLIC]]
  ret i16 %x
}

; A naked assembly body implements the public ABI itself, even when local.
define internal void @naked_entry() naked {
; POLICY: define internal void @naked_entry() #[[NAKED:[0-9]+]]
  call void asm sideeffect "ret s7", ""()
  unreachable
}
define void @call_naked() {
  call void @naked_entry()
  ret void
}

; POLICY: attributes #[[PUBLIC]] = { "riscc-link-register"="s7"
; POLICY: attributes #[[PRIVATE]] = { "riscc-link-register"="s3"
; POLICY: attributes #[[NAKED]] = { naked "riscc-link-register"="s7"
