; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s

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
; CHECK:       mfs r0, s5
; CHECK:       st r0,
; CHECK:       mfs r0, s6
; CHECK:       st r0,
; CHECK:       ld r0,
; CHECK:       mts s6, r0
; CHECK:       ld r0,
; CHECK:       mts s5, r0
; CHECK:       addi r7, 4
; CHECK:       ret s7
  call void asm sideeffect "", "~{s5},~{s6}"()
  ret void
}

; The expanded caller-saved cache pool holds a short-lived allocator spill
; while S3/S4 hold two callee-saved GPR entry values and S7 holds the link.
define i16 @leaf_local_spill(i16 %a, i16 %b, i16 %c, i32 %value, i16 %suffix) {
; CHECK-LABEL: leaf_local_spill:
; CHECK:       addi r7, -4
; CHECK:       mts s3, r4
; CHECK:       mts s4, r5
; CHECK:       st r6,
; CHECK:       mts s2,
; CHECK:       mfs r1, s2
; CHECK:       ld r6,
; CHECK:       mfs r5, s4
; CHECK:       mfs r4, s3
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
; CHECK:       ret s7
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
