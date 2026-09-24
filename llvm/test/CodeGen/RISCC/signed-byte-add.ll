; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=full -verify-machineinstrs -o - | FileCheck %s
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=min -verify-machineinstrs -o - | FileCheck %s

; Signed byte arithmetic wraps at bit 8. Fold the sign-extension bias into
; a single-use addition; do not duplicate a shared addition or its carry bits.

; CHECK-LABEL: increment:
; CHECK: addi r1, -127
; CHECK-NEXT: andi r1, 255
; CHECK-NEXT: addi r1, -128
; CHECK-NEXT: ret s7
define i32 @increment(i32 %x) {
 %sum = add i32 %x, 1
 %byte = trunc i32 %sum to i8
 %result = sext i8 %byte to i32
 ret i32 %result
}

; CHECK-LABEL: decrement:
; CHECK: addi r1, 127
; CHECK-NEXT: andi r1, 255
; CHECK-NEXT: addi r1, -128
; CHECK-NEXT: ret s7
define i32 @decrement(i32 %x) {
 %sum = add i32 %x, -1
 %byte = trunc i32 %sum to i8
 %result = sext i8 %byte to i32
 ret i32 %result
}

; CHECK-LABEL: add127:
; CHECK: addi r1, -1
; CHECK-NEXT: andi r1, 255
; CHECK-NEXT: addi r1, -128
; CHECK-NEXT: ret s7
define i32 @add127(i32 %x) {
 %sum = add i32 %x, 127
 %byte = trunc i32 %sum to i8
 %result = sext i8 %byte to i32
 ret i32 %result
}

; CHECK-LABEL: subtract128:
; CHECK-NOT: addi
; CHECK: andi r1, 255
; CHECK-NEXT: addi r1, -128
; CHECK-NEXT: ret s7
define i32 @subtract128(i32 %x) {
 %sum = add i32 %x, -128
 %byte = trunc i32 %sum to i8
 %result = sext i8 %byte to i32
 ret i32 %result
}

; CHECK-LABEL: subtract127:
; CHECK: addi r1, 1
; CHECK-NEXT: andi r1, 255
; CHECK-NEXT: addi r1, -128
; CHECK-NEXT: ret s7
define i32 @subtract127(i32 %x) {
 %sum = add i32 %x, -127
 %byte = trunc i32 %sum to i8
 %result = sext i8 %byte to i32
 ret i32 %result
}

; CHECK-LABEL: shared:
; CHECK: addi r1, 1
; CHECK: st r1,
; CHECK: andi r1, 255
; CHECK-NEXT: xori r1, 128
; CHECK-NEXT: addi r1, -128
; CHECK: ret s7
define i32 @shared(i32 %x, ptr %out) {
 %sum = add i32 %x, 1
 store volatile i32 %sum, ptr %out
 %byte = trunc i32 %sum to i8
 %result = sext i8 %byte to i32
 ret i32 %result
}

; The unextended sum must also survive when it is used after the extension.
; CHECK-LABEL: shared_after:
; CHECK: addi {{r[0-6]}}, 1
; CHECK: xori {{r[0-6]}}, 128
; CHECK: ret s7
define i32 @shared_after(i32 %x) {
 %sum = add i32 %x, 1
 %byte = trunc i32 %sum to i8
 %extended = sext i8 %byte to i32
 %result = add i32 %sum, %extended
 ret i32 %result
}
