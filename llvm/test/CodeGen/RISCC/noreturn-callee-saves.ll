; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefixes=CHECK,MAIN
; RUN: llc -mtriple=riscc -mcpu=min -verify-machineinstrs %s -o - | FileCheck %s --check-prefixes=CHECK,MAIN
; RUN: llc -mtriple=riscc -mcpu=nano -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=CHECK,MAIN
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=CHECK,MAIN

declare void @stop() noreturn nounwind
declare void @may_throw() noreturn
declare void @ordinary_call()

; Neither stack saves nor S-cache reservations are necessary when the entry
; values cannot be observed by a caller or unwinder.
define void @no_return_clobber() noreturn nounwind {
; CHECK-LABEL: no_return_clobber:
; CHECK-NOT:   st {{r[4-6]}},
; CHECK-NOT:   mts {{s[0-7]}}, {{r[4-6]}}
; CHECK-NOT:   addi r7,
; CHECK:       .size no_return_clobber,
  call void asm sideeffect "", "~{r4},~{r5},~{r6}"()
  call void @stop()
  unreachable
}

; A potentially throwing noreturn function still needs the incoming values.
define void @unwinding_clobber() noreturn {
; CHECK-LABEL: unwinding_clobber:
; CHECK-DAG:   st r4,
; CHECK-DAG:   st r5,
; MAIN-DAG:    st r6,
; CHECK:       .size unwinding_clobber,
  call void asm sideeffect "", "~{r4},~{r5},~{r6}"()
  call void @may_throw()
  unreachable
}

define void @unwind_table_clobber() noreturn nounwind uwtable {
; CHECK-LABEL: unwind_table_clobber:
; CHECK-DAG:   st r4,
; CHECK-DAG:   st r5,
; MAIN-DAG:    st r6,
; CHECK:       .size unwind_table_clobber,
  call void asm sideeffect "", "~{r4},~{r5},~{r6}"()
  call void @stop()
  unreachable
}

define void @returning_clobber() nounwind {
; CHECK-LABEL: returning_clobber:
; CHECK-DAG:   st r4,
; CHECK-DAG:   st r5,
; MAIN-DAG:    st r6,
; CHECK:       .size returning_clobber,
  call void asm sideeffect "", "~{r4},~{r5},~{r6}"()
  call void @ordinary_call()
  ret void
}

define void @no_return_cache_clobber() noreturn nounwind {
; CHECK-LABEL: no_return_cache_clobber:
; CHECK-NOT:   mfs
; CHECK-NOT:   st {{r[0-6]}},
; CHECK-NOT:   addi r7,
; CHECK:       .size no_return_cache_clobber,
  call void asm sideeffect "", "~{s5},~{s6}"()
  call void @stop()
  unreachable
}

define void @interrupt_clobber() noreturn nounwind "interrupt" {
; CHECK-LABEL: interrupt_clobber:
; CHECK-DAG:   st r4,
; CHECK-DAG:   st r5,
; MAIN-DAG:    st r6,
; CHECK:       .size interrupt_clobber,
  call void asm sideeffect "", "~{r4},~{r5},~{r6}"()
  call void @stop()
  unreachable
}
