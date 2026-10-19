; REQUIRES: riscc-registered-target
; RUN: opt -mtriple=riscc -mcpu=full -passes='print<cost-model>' -cost-kind=code-size -disable-output %s 2>&1 | \
; RUN:   FileCheck %s --check-prefixes=CHECK,RC16
; RUN: sed 's/p:16:16/p:32:32/' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 -passes='print<cost-model>' \
; RUN:   -cost-kind=code-size -disable-output - 2>&1 | FileCheck %s --check-prefixes=CHECK,RC32

target datalayout = "e-p:16:16"

; A byte extension needs three compact instructions on either native width.
define i16 @size_byte(i8 %v) optsize {
; CHECK-LABEL: for function 'size_byte'
; CHECK: cost of 3 for instruction: %wide = sext i8 %v to i16
  %wide = sext i8 %v to i16
  ret i16 %wide
}

; Keep the speed policy unchanged.
define i16 @speed_byte(i8 %v) {
; CHECK-LABEL: for function 'speed_byte'
; CHECK: cost of 1 for instruction: %wide = sext i8 %v to i16
  %wide = sext i8 %v to i16
  ret i16 %wide
}

; Full's RC32 halfword extension is four shifts. RC16's wide result is left
; to the generic legalization cost instead of pretending it is native.
define i32 @size_half(i16 %v) optsize {
; CHECK-LABEL: for function 'size_half'
; RC16: cost of 1 for instruction: %wide = sext i16 %v to i32
; RC32: cost of 4 for instruction: %wide = sext i16 %v to i32
  %wide = sext i16 %v to i32
  ret i32 %wide
}

; Extending loads remain free; they do not need the register-only sequence.
define i16 @size_loaded_byte(ptr %p) optsize {
; CHECK-LABEL: for function 'size_loaded_byte'
; CHECK: cost of 0 for instruction: %wide = sext i8 %v to i16
  %v = load i8, ptr %p
  %wide = sext i8 %v to i16
  ret i16 %wide
}
