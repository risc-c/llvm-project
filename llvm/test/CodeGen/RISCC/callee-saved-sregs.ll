; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,MIN

target triple = "riscc-none-elf"

declare void @external()

; A mainline leaf can preserve callee-saved GPRs in the software-managed
; caller-saved cache bank.
define void @leaf_clobber_r5_r6() {
; CHECK-LABEL: leaf_clobber_r5_r6:
; CHECK:       mts s2, r5
; CHECK-NEXT:  mts s3, r6
; CHECK:       mfs r6, s3
; CHECK-NEXT:  mfs r5, s2
; CHECK-NEXT:  ret s7
  call void asm sideeffect "", "~{r5},~{r6}"()
  ret void
}

; S5 and S6 themselves are callee-saved.  Unlike the caller-saved cache
; registers, an inline-assembly clobber requires ordinary entry/exit saves.
define void @leaf_clobber_s5_s6() {
; CHECK-LABEL: leaf_clobber_s5_s6:
; CHECK:       addi r7, -4
; CHECK-DAG:   mfs [[SAVE5:r[0-6]]], s5
; CHECK-DAG:   st [[SAVE5]], [r7 + 2]
; CHECK-DAG:   mfs [[SAVE6:r[0-6]]], s6
; CHECK-DAG:   st [[SAVE6]], [r7 + 0]
; CHECK-DAG:   ld [[S6:r[0-6]]], [r7 + 0]
; CHECK-DAG:   mts s6, [[S6]]
; CHECK-DAG:   ld [[S5:r[0-6]]], [r7 + 2]
; CHECK-DAG:   mts s5, [[S5]]
; CHECK:       addi r7, 4
; CHECK:       ret s7
  call void asm sideeffect "", "~{s5},~{s6}"()
  ret void
}

; A leaf under register pressure saves all three callee-saved GPRs in the
; caller-saved cache bank, leaving S7 for the link.
define i16 @leaf_local_spill(i16 %a, i16 %b, i16 %c, i32 %value, i16 %suffix) {
; CHECK-LABEL: leaf_local_spill:
; CHECK-NOT:   addi r7,
; CHECK-DAG:   mts s2,
; CHECK-DAG:   mts s3,
; CHECK-DAG:   mts s4,
; CHECK:       mfs r6, s4
; CHECK:       mfs r5, s3
; CHECK:       mfs r4, s2
; CHECK-NOT:   addi r7,
; CHECK:       ret s7
  %a.ok = icmp eq i16 %a, 1
  %b.ok = icmp eq i16 %b, 2
  %ab.ok = and i1 %a.ok, %b.ok
  %c.ok = icmp eq i16 %c, 3
  %abc.ok = and i1 %ab.ok, %c.ok
  %value.ok = icmp eq i32 %value, 858989090
  %args.ok = select i1 %abc.ok, i1 %value.ok, i1 false
  %suffix.ok = icmp eq i16 %suffix, 4
  %all.ok = and i1 %args.ok, %suffix.ok
  %result = select i1 %all.ok, i16 18058, i16 0
  ret i16 %result
}

; Compiler-private shift staircases preserve the cache. The Min direct-call
; expansion uses the caller-saved scratch register r0 as its target.
define i16 @shift_call_clobber_r5_r6(i16 %value) minsize {
; CHECK-LABEL: shift_call_clobber_r5_r6:
; MIN:         ldi16 r0, __riscc_shlhi11
; MIN-NEXT:    jalr s7, r0
; FULL:        ret s7
; MIN:         ret s2
  call void asm sideeffect "", "~{r5},~{r6}"()
  %result = shl i16 %value, 11
  ret i16 %result
}

; An ordinary C call may clobber the entire cache, so its caller continues to
; preserve callee-saved GPRs on the data stack.
define void @ordinary_call_clobber_r5_r6() {
; CHECK-LABEL: ordinary_call_clobber_r5_r6:
; CHECK-NOT:   mts s3, r5
; CHECK-NOT:   mts s4, r6
; CHECK:       st r5,
; CHECK:       st r6,
; CHECK:       ld r6,
; CHECK:       ld r5,
; CHECK:       ret s7
  call void asm sideeffect "", "~{r5},~{r6}"()
  call void @external()
  ret void
}
