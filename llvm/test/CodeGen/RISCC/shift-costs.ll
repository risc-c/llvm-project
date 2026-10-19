; REQUIRES: riscc-registered-target
; RUN: opt -mtriple=riscc -mcpu=full -passes='print<cost-model>' -disable-output %s 2>&1 | FileCheck %s \
; RUN:   --check-prefix=FULL16
; RUN: opt -mtriple=riscc -mcpu=min -passes='print<cost-model>' -disable-output %s 2>&1 | FileCheck %s \
; RUN:   --check-prefix=MIN16
; RUN: opt -mtriple=riscc -mcpu=nano -passes='print<cost-model>' -disable-output %s 2>&1 | FileCheck %s \
; RUN:   --check-prefix=NANO
; RUN: sed 's/p:16:16/p:32:32/' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 -passes='print<cost-model>' \
; RUN:   -disable-output - 2>&1 | FileCheck %s --check-prefix=FULL32
; RUN: sed 's/p:16:16/p:32:32/' %s | opt -mtriple=riscc -mcpu=min -mattr=+rc32 -passes='print<cost-model>' \
; RUN:   -disable-output - 2>&1 | FileCheck %s --check-prefix=MIN32
; RUN: sed 's/p:16:16/p:32:32/' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 -cost-kind=code-size \
; RUN:   -passes='print<cost-model>' -disable-output - 2>&1 | FileCheck %s --check-prefix=FULL32

target datalayout = "e-p:16:16"

; Constant shifts are costed by their native instruction count, not as one
; generic custom operation. A sign mask can use compare-and-negate except
; in Nano; Full size builds retain the shorter immediate-shift sequence.

define i16 @shifts16(i16 %x) {
; FULL16: cost of 1 for instruction: %a = shl i16 %x, 1
; FULL16: cost of 1 for instruction: %b = lshr i16 %x, 8
; FULL16: cost of 2 for instruction: %c = lshr i16 %x, 12
; FULL16: cost of 3 for instruction: %d = ashr i16 %x, 15
; MIN16: cost of 1 for instruction: %a = shl i16 %x, 1
; MIN16: cost of 8 for instruction: %b = lshr i16 %x, 8
; MIN16: cost of 12 for instruction: %c = lshr i16 %x, 12
; MIN16: cost of 3 for instruction: %d = ashr i16 %x, 15
; NANO: cost of 1 for instruction: %a = shl i16 %x, 1
; NANO: cost of 8 for instruction: %b = lshr i16 %x, 8
; NANO: cost of 12 for instruction: %c = lshr i16 %x, 12
; NANO: cost of 15 for instruction: %d = ashr i16 %x, 15
  %a = shl i16 %x, 1
  %b = lshr i16 %x, 8
  %c = lshr i16 %x, 12
  %d = ashr i16 %x, 15
  ret i16 %d
}

define i16 @size16(i16 %x) optsize {
; FULL16: cost of 2 for instruction: %sign = ashr i16 %x, 15
; MIN16: cost of 3 for instruction: %sign = ashr i16 %x, 15
; NANO: cost of 15 for instruction: %sign = ashr i16 %x, 15
  %sign = ashr i16 %x, 15
  ret i16 %sign
}

define i32 @shifts32(i32 %x) {
; FULL32: cost of 1 for instruction: %a = shl i32 %x, 1
; FULL32: cost of 1 for instruction: %b = lshr i32 %x, 8
; FULL32: cost of 3 for instruction: %c = lshr i32 %x, 24
; FULL32: cost of 3 for instruction: %d = ashr i32 %x, 31
; MIN32: cost of 1 for instruction: %a = shl i32 %x, 1
; MIN32: cost of 8 for instruction: %b = lshr i32 %x, 8
; MIN32: cost of 24 for instruction: %c = lshr i32 %x, 24
; MIN32: cost of 3 for instruction: %d = ashr i32 %x, 31
  %a = shl i32 %x, 1
  %b = lshr i32 %x, 8
  %c = lshr i32 %x, 24
  %d = ashr i32 %x, 31
  ret i32 %d
}

define i32 @size32(i32 %x) optsize {
; FULL32: cost of 4 for instruction: %sign = ashr i32 %x, 31
; MIN32: cost of 3 for instruction: %sign = ashr i32 %x, 31
  %sign = ashr i32 %x, 31
  ret i32 %sign
}
