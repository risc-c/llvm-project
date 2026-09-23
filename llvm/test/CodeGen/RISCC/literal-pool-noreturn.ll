; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O0 -filetype=obj %s -o %t
; RUN: llc -enable-new-pm -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O0 -verify-machineinstrs -filetype=obj %s -o %t.new-pm

declare void @stop(i32) noreturn

; A noreturn call has no CFG successor, but is not itself a barrier. Its
; literal pool can follow the call without a branch around the data.
define void @long_noreturn() {
; CHECK-LABEL: long_noreturn:
; CHECK:       .zero 600
; CHECK:       ldpc r0, [[TARGET:.Ltmp[0-9]+]]
; CHECK-NEXT:  jalr s7, r0
; CHECK-NOT:   jmp8
; CHECK:       [[TARGET]]:
; CHECK-NEXT:  .long call_target(stop)
  call void asm sideeffect ".space 600", ""()
  call void @stop(i32 305419896)
  unreachable
}
