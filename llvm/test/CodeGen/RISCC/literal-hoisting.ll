; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=sys -mattr=+rc32 -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=MIR

; Preserve the constant-pool memory operand during selection so MachineLICM
; can move both literal loads to the preheader, subject to register pressure.
; MIR: LDPC %const.0 :: (load (s32) from constant-pool)
; MIR: LDPC %const.1 :: (load (s32) from constant-pool)

define i32 @loop_xor(ptr %p, i32 %n) {
; CHECK-LABEL: loop_xor:
; CHECK:       ldpc [[XOR:r[0-7]]],
; CHECK:       ldpc [[STEP:r[0-7]]],
; CHECK:       [[LOOP:.LBB[0-9_]+]]:
; CHECK-NOT:   ldpc
; CHECK:       xor {{r[0-7]}}, {{r[0-7]}}, [[XOR]]
; CHECK-NOT:   ldpc
; CHECK:       add {{r[0-7]}}, {{r[0-7]}}, [[STEP]]
; CHECK-NOT:   ldpc
; CHECK:       bnez [[LOOP]]
entry:
  br label %loop
loop:
  %ptr = phi ptr [ %p, %entry ], [ %next, %loop ]
  %count = phi i32 [ %n, %entry ], [ %remaining, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %total, %loop ]
  %value = load volatile i32, ptr %ptr
  %mixed = xor i32 %value, 305419896
  %total = add i32 %sum, %mixed
  %next = getelementptr i32, ptr %ptr, i32 1
  %remaining = add i32 %count, -1
  %again = icmp ne i32 %remaining, 0
  br i1 %again, label %loop, label %exit
exit:
  ret i32 %total
}
