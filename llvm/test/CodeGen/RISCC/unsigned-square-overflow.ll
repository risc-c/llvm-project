; REQUIRES: riscc-registered-target
; RUN: opt -verify-each -passes=riscc-prepare -S %s | FileCheck %s --check-prefix=IR16
; RUN: sed 's/p:16:16/p:32:32/' %s | opt -verify-each -passes=riscc-prepare -S - | FileCheck %s --check-prefix=IR32
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM16
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM32

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-n8:16-S16"
target triple = "riscc-none-elf"

declare {i16, i1} @llvm.umul.with.overflow.i16(i16, i16)
declare {i32, i1} @llvm.umul.with.overflow.i32(i32, i32)
declare {i16, i1} @llvm.smul.with.overflow.i16(i16, i16)

define {i16, i1} @square16(i16 %x) {
; IR16-LABEL: define { i16, i1 } @square16
; IR16: %[[X:.*]] = freeze i16 %x
; IR16: %[[P:.*]] = mul i16 %[[X]], %[[X]]
; IR16: %[[O:.*]] = icmp ugt i16 %[[X]], 255
; IR16: insertvalue { i16, i1 } poison, i16 %[[P]], 0
; IR16: insertvalue { i16, i1 } {{.*}}, i1 %[[O]], 1
; IR32-LABEL: define { i16, i1 } @square16
; IR32: call { i16, i1 } @llvm.umul.with.overflow.i16
; ASM16-LABEL: square16:
; ASM16-NOT: jal
; ASM16: mul
; ASM16-NOT: jal
; ASM16: ret
  %r = call {i16, i1} @llvm.umul.with.overflow.i16(i16 %x, i16 %x)
  ret {i16, i1} %r
}

define {i32, i1} @square32(i32 %x) {
; IR32-LABEL: define { i32, i1 } @square32
; IR32: %[[X:.*]] = freeze i32 %x
; IR32: %[[P:.*]] = mul i32 %[[X]], %[[X]]
; IR32: %[[O:.*]] = icmp ugt i32 %[[X]], 65535
; IR32: insertvalue { i32, i1 } poison, i32 %[[P]], 0
; IR32: insertvalue { i32, i1 } {{.*}}, i1 %[[O]], 1
; IR16-LABEL: define { i32, i1 } @square32
; IR16: call { i32, i1 } @llvm.umul.with.overflow.i32
; ASM32-LABEL: square32:
; ASM32-NOT: jal
; ASM32: mul
; ASM32-NOT: jal
; ASM32: ret
  %r = call {i32, i1} @llvm.umul.with.overflow.i32(i32 %x, i32 %x)
  ret {i32, i1} %r
}

; Independent operands and signed overflow do not obey the square identity.
define {i16, i1} @product16(i16 %a, i16 %b) {
; IR16-LABEL: define { i16, i1 } @product16
; IR16: call { i16, i1 } @llvm.umul.with.overflow.i16(i16 %a, i16 %b)
  %r = call {i16, i1} @llvm.umul.with.overflow.i16(i16 %a, i16 %b)
  ret {i16, i1} %r
}

define {i16, i1} @signed_square16(i16 %x) {
; IR16-LABEL: define { i16, i1 } @signed_square16
; IR16: call { i16, i1 } @llvm.smul.with.overflow.i16(i16 %x, i16 %x)
  %r = call {i16, i1} @llvm.smul.with.overflow.i16(i16 %x, i16 %x)
  ret {i16, i1} %r
}
