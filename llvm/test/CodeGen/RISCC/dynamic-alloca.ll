; REQUIRES: riscc-registered-target
; RUN: not --crash llc -mtriple=riscc-none-elf -mcpu=full < %s 2>&1 | FileCheck %s

; CHECK: RISC-C does not support dynamic stack allocation

define ptr @dynamic_alloca(i16 %count) {
  %allocation = alloca i8, i16 %count
  ret ptr %allocation
}
