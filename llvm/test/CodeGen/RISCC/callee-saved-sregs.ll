; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s

target triple = "riscc-none-elf"

; A mainline leaf can preserve both callee-saved GPRs in the software-managed
; S register bank. This must not create a data-stack frame.
define void @leaf_clobber_r5_r6() {
; CHECK-LABEL: leaf_clobber_r5_r6:
; CHECK:       mts s3, r5
; CHECK-NEXT:  mts s4, r6
; CHECK-NOT:   addi r7
; CHECK:       mfs r6, s4
; CHECK-NEXT:  mfs r5, s3
; CHECK-NEXT:  rets
  call void asm sideeffect "", "~{r5},~{r6}"()
  ret void
}

; The same pool can hold a short-lived allocator spill after the two
; callee-saved backups. The spill gets first choice because it may execute
; repeatedly; the entry/exit-only backups use the remaining registers.
define i16 @leaf_local_spill(i16 %a, i16 %b, i16 %c, i32 %value, i16 %suffix) {
; CHECK-LABEL: leaf_local_spill:
; CHECK:       mts s4, r5
; CHECK-NEXT:  mts s5, r6
; CHECK:       mts s3,
; CHECK-NOT:   stw
; CHECK:       mfs r1, s3
; CHECK:       mfs r6, s5
; CHECK-NEXT:  mfs r5, s4
; CHECK:       rets
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
