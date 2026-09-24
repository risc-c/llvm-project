; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc -mcpu=full -O2 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC16

; Scalar stores exposed by memcpy expansion can be dead even when other
; stores intervene. Observation, volatility and partial overwrites prevent
; elimination. Both widths retain every live byte of the copy.
target triple = "riscc-none-elf"
declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1 immarg)
; RC32-LABEL: overwritten_field:
; RC32-COUNT-5: {{^[ \t]*ld[ \t]}}
; RC32-NOT: {{^[ \t]*ld[ \t]}}
; RC32: ret s7
; RC16-LABEL: overwritten_field:
; RC16-COUNT-12: {{^[ \t]*ld[ \t]}}
; RC16-NOT: {{^[ \t]*ld[ \t]}}
; RC16: ret s7
define void @overwritten_field(ptr %dst, ptr %src, ptr %other) {
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 24, i1 false)
  store i32 11, ptr %other
  %field = getelementptr i8, ptr %dst, i32 12
  store i32 22, ptr %field
  ret void
}
; RC32-LABEL: observed_field:
; RC32-COUNT-7: {{^[ \t]*ld[ \t]}}
; RC32-NOT: {{^[ \t]*ld[ \t]}}
; RC32: ret s7
; RC16-LABEL: observed_field:
; RC16-COUNT-14: {{^[ \t]*ld[ \t]}}
; RC16-NOT: {{^[ \t]*ld[ \t]}}
; RC16: ret s7
define void @observed_field(ptr %dst, ptr %src, ptr %out) {
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 24, i1 false)
  %field = getelementptr i8, ptr %dst, i32 12
  %old = load volatile i32, ptr %field
  store i32 %old, ptr %out
  store i32 22, ptr %field
  ret void
}
; RC32-LABEL: partial_field:
; RC32-COUNT-6: {{^[ \t]*ld[ \t]}}
; RC32-NOT: {{^[ \t]*ld[ \t]}}
; RC32: ret s7
; RC16-LABEL: partial_field:
; RC16-COUNT-12: {{^[ \t]*ld[ \t]}}
; RC16-NOT: {{^[ \t]*ld[ \t]}}
; RC16: ret s7
define void @partial_field(ptr %dst, ptr %src, ptr %other) {
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 24, i1 false)
  store i32 11, ptr %other
  %field = getelementptr i8, ptr %dst, i32 12
  store i8 22, ptr %field
  ret void
}
; RC32-LABEL: volatile_copy:
; RC32-COUNT-6: {{^[ \t]*ld[ \t]}}
; RC32-NOT: {{^[ \t]*ld[ \t]}}
; RC32: ret s7
; RC16-LABEL: volatile_copy:
; RC16-COUNT-12: {{^[ \t]*ld[ \t]}}
; RC16-NOT: {{^[ \t]*ld[ \t]}}
; RC16: ret s7
define void @volatile_copy(ptr %dst, ptr %src, ptr %other) {
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 24, i1 true)
  store i32 11, ptr %other
  %field = getelementptr i8, ptr %dst, i32 12
  store i32 22, ptr %field
  ret void
}

; A native RC16 field can be removed without combining split-word stores.
; RC16-LABEL: halfword_field:
; RC16-COUNT-5: {{^[ \t]*ld[ \t]}}
; RC16-NOT: {{^[ \t]*ld[ \t]}}
; RC16: ret s7
define void @halfword_field(ptr %dst, ptr %src, ptr %other) {
  call void @llvm.memcpy.p0.p0.i32(ptr align 2 %dst, ptr align 2 %src, i32 12, i1 false)
  store i16 11, ptr %other
  %field = getelementptr i8, ptr %dst, i32 4
  store i16 22, ptr %field
  ret void
}
