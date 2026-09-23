; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 --combiner-disabled -verify-machineinstrs < %s | FileCheck %s

; Keep an RC16 word address in the form base + (index + displacement).  The
; inner sum is a one-use address node; selecting it must create a MachineADD,
; rather than adding an unselected ISD::ADD to the DAG.
target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

@table = internal global [8 x i16] zeroinitializer, align 2

define i16 @nested_global_load(ptr %base) {
entry:
  %element = getelementptr [8 x i16], ptr @table, i16 0, i16 1
  %offset = ptrtoint ptr %element to i16
  %baseint = ptrtoint ptr %base to i16
  %addressint = add i16 %baseint, %offset
  %address = inttoptr i16 %addressint to ptr
  %value = load volatile i16, ptr %address, align 2
  ret i16 %value
}
; CHECK-LABEL: nested_global_load:
; CHECK:       ldi16 [[GLOBAL:r[0-7]]], table
; CHECK:       add [[ADDRESS:r[0-7]]], r1, [[GLOBAL]]
; CHECK:       ld r1, {{\[}}[[ADDRESS]] + 2{{\]}}
; CHECK:       ret s7

define void @nested_global_store(ptr %base, i16 %value) {
entry:
  %element = getelementptr [8 x i16], ptr @table, i16 0, i16 1
  %offset = ptrtoint ptr %element to i16
  %baseint = ptrtoint ptr %base to i16
  %addressint = add i16 %baseint, %offset
  %address = inttoptr i16 %addressint to ptr
  store volatile i16 %value, ptr %address, align 2
  ret void
}
; CHECK-LABEL: nested_global_store:
; CHECK:       ldi16 [[GLOBAL:r[0-7]]], table
; CHECK:       add [[ADDRESS:r[0-7]]], r1, [[GLOBAL]]
; CHECK:       st r2, {{\[}}[[ADDRESS]] + 2{{\]}}
; CHECK:       ret s7
