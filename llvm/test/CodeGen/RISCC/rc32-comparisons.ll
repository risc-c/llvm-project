; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"
target triple = "riscc-none-elf"

define i32 @branch_slt_zero(i32 %x) {
; CHECK-LABEL: branch_slt_zero:
; CHECK:       mov r0, r1
; CHECK-NEXT:  bltz
  %cmp = icmp slt i32 %x, 0
  br i1 %cmp, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}

define i32 @branch_sge_zero(i32 %x) {
; CHECK-LABEL: branch_sge_zero:
; CHECK:       mov r0, r1
; CHECK-NEXT:  bltz
  %cmp = icmp sge i32 %x, 0
  br i1 %cmp, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}

define i32 @branch_sgt_neg1(i32 %x) {
; CHECK-LABEL: branch_sgt_neg1:
; CHECK:       mov r0, r1
; CHECK-NEXT:  bltz
  %cmp = icmp sgt i32 %x, -1
  br i1 %cmp, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}

define i32 @branch_sle_neg1(i32 %x) {
; CHECK-LABEL: branch_sle_neg1:
; CHECK:       mov r0, r1
; CHECK-NEXT:  bgez
  %cmp = icmp sle i32 %x, -1
  br i1 %cmp, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}

define i32 @branch_eq_neg128(i32 %x) {
; CHECK-LABEL: branch_eq_neg128:
; CHECK:       cmpi r1, -128
; CHECK-NEXT:  bnez
  %cmp = icmp eq i32 %x, -128
  br i1 %cmp, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}

define i32 @branch_eq_pos127(i32 %x) {
; CHECK-LABEL: branch_eq_pos127:
; CHECK:       cmpi r1, 127
; CHECK-NEXT:  bnez
  %cmp = icmp eq i32 %x, 127
  br i1 %cmp, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}

define i32 @branch_eq_out_pos128(i32 %x) {
; CHECK-LABEL: branch_eq_out_pos128:
; CHECK:       ldi r{{[0-7]}}, 128
; CHECK-NEXT:  sub r{{[0-7]}}, r1, r{{[0-7]}}
; CHECK-NEXT:  bnez
  %cmp = icmp eq i32 %x, 128
  br i1 %cmp, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}

define i32 @branch_eq_out_neg129(i32 %x) {
; CHECK-LABEL: branch_eq_out_neg129:
; CHECK:       ldpc r{{[0-7]}},
; CHECK:       sub r{{[0-7]}}, r1, r{{[0-7]}}
; CHECK-NEXT:  bnez
  %cmp = icmp eq i32 %x, -129
  br i1 %cmp, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}

define i32 @branch_ne_pos127(i32 %x) {
; CHECK-LABEL: branch_ne_pos127:
; CHECK:       cmpi r1, 127
; CHECK-NEXT:  beqz
  %cmp = icmp ne i32 %x, 127
  br i1 %cmp, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}

define i32 @eq_zero(i32 %x) {
; CHECK-LABEL: eq_zero:
; CHECK:       ldi [[ONE:r[0-7]]], 1
; CHECK-NEXT:  sltu r1, r1, [[ONE]]
; CHECK-NEXT:  ret
  %cmp = icmp eq i32 %x, 0
  %result = zext i1 %cmp to i32
  ret i32 %result
}

define i32 @eq_u8_255(i32 %x) {
; CHECK-LABEL: eq_u8_255:
; CHECK:       xori r1, 255
; CHECK-NEXT:  ldi [[ONE:r[0-7]]], 1
; CHECK-NEXT:  sltu r1, r1, [[ONE]]
; CHECK-NEXT:  ret
  %cmp = icmp eq i32 %x, 255
  %result = zext i1 %cmp to i32
  ret i32 %result
}

define i32 @ne_u8_255(i32 %x) {
; CHECK-LABEL: ne_u8_255:
; CHECK:       xori r{{[0-7]}}, 255
; CHECK:       ldi r{{[0-7]}}, 0
; CHECK:       sltu r1, r{{[0-7]}}, r{{[0-7]}}
  %cmp = icmp ne i32 %x, 255
  %result = zext i1 %cmp to i32
  ret i32 %result
}
