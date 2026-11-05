; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+vsh -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32,+vsh -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc -mcpu=sys -mattr=+vsh -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC16

define i16 @left16(i16 %x, i16 %n) {
; RC16-LABEL: left16:
; RC16: sll r1, r2
; RC16-NEXT: ret s7
  %r = shl i16 %x, %n
  ret i16 %r
}

define i16 @right16(i16 %x, i16 %n) {
; RC16-LABEL: right16:
; RC16: srl r1, r2
; RC16-NEXT: ret s7
  %r = lshr i16 %x, %n
  ret i16 %r
}

define i16 @signed16(i16 %x, i16 %n) {
; RC16-LABEL: signed16:
; RC16: sra r1, r2
; RC16-NEXT: ret s7
  %r = ashr i16 %x, %n
  ret i16 %r
}

define i16 @selfcount16(i16 %x) {
; RC16-LABEL: selfcount16:
; RC16-NOT: andi
; RC16: sll r1, r1
; RC16-NEXT: ret s7
  %n = and i16 %x, 15
  %r = shl i16 %x, %n
  ret i16 %r
}

define i32 @left32(i32 %x, i32 %n) {
; RC32-LABEL: left32:
; RC32: sll r1, r2
; RC32-NEXT: ret s7
  %r = shl i32 %x, %n
  ret i32 %r
}

define i32 @right32(i32 %x, i32 %n) {
; RC32-LABEL: right32:
; RC32: srl r1, r2
; RC32-NEXT: ret s7
  %r = lshr i32 %x, %n
  ret i32 %r
}

define i32 @signed32(i32 %x, i32 %n) {
; RC32-LABEL: signed32:
; RC32: sra r1, r2
; RC32-NEXT: ret s7
  %r = ashr i32 %x, %n
  ret i32 %r
}

define i32 @selfcount32(i32 %x) {
; RC32-LABEL: selfcount32:
; RC32-NOT: andi
; RC32: sll r1, r1
; RC32-NEXT: ret s7
  %n = and i32 %x, 31
  %r = shl i32 %x, %n
  ret i32 %r
}

define i32 @byte_bit(i32 %x, i32 %n) {
; RC32-LABEL: byte_bit:
; RC32: andi r2, 7
; RC32: srl r1, r2
; RC32: andi r1, 1
; RC32-NEXT: ret s7
  %n8 = and i32 %n, 7
  %shift = lshr i32 %x, %n8
  %r = and i32 %shift, 1
  ret i32 %r
}

define i32 @constant32(i32 %x) {
; RC32-LABEL: constant32:
; RC32: slli [[TEMP:r[0-6]]], r1, 8
; RC32: slli r1, [[TEMP]], 1
; RC32-NEXT: ret s7
  %r = shl i32 %x, 9
  ret i32 %r
}

define i32 @rotate_left32(i32 %x, i32 %n) {
; RC32-LABEL: rotate_left32:
; RC32-NOT: call
; RC32-DAG: sll
; RC32-DAG: srl
; RC32: or
; RC32: ret s7
  %r = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 %n)
  ret i32 %r
}

define i16 @rotate_right16(i16 %x, i16 %n) {
; RC16-LABEL: rotate_right16:
; RC16-NOT: call
; RC16-DAG: sll
; RC16-DAG: srl
; RC16: or
; RC16: ret s7
  %r = call i16 @llvm.fshr.i16(i16 %x, i16 %x, i16 %n)
  ret i16 %r
}

define i32 @funnel_left32(i32 %x, i32 %y, i32 %n) {
; RC32-LABEL: funnel_left32:
; RC32-NOT: call
; RC32-DAG: sll
; RC32-DAG: srl
; RC32: or
; RC32: ret s7
  %r = call i32 @llvm.fshl.i32(i32 %x, i32 %y, i32 %n)
  ret i32 %r
}

declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i16 @llvm.fshr.i16(i16, i16, i16)
