; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 %s -o - | FileCheck %s --check-prefix=RC32
; RUN: sed -e 's/p:32:32/p:16:16/' -e 's/i32:32/i16:16/' -e 's/i32/i16/g' -e 's/align 4/align 2/g' %s | llc -mtriple=riscc -mcpu=full -o - | FileCheck %s --check-prefix=RC16

target datalayout = "e-p:32:32-i32:32-n8:16:32"
target triple = "riscc-none-elf"

; Related row/column addresses share a base. Only one row multiplication is
; needed; adjacent elements use the native word displacement. The reload of
; the first array must remain because the two arrays can alias.
define void @related_addresses(ptr %a, ptr %b, i32 %index, i32 %value) {
; RC32-LABEL: related_addresses:
; RC32: mul
; RC32-NOT: mul
; RC32: ld {{r[0-7]}}, [{{r[0-7]}} + -4]
; RC32-NOT: mul
; RC32: ret
; RC16-LABEL: related_addresses:
; RC16: mul
; RC16-NOT: mul
; RC16: ld {{r[0-7]}}, [{{r[0-7]}} + -2]
; RC16-NOT: mul
; RC16: ret
entry:
  %i = add i32 %index, 3
  %a0 = getelementptr i32, ptr %a, i32 %i
  store i32 %value, ptr %a0, align 4
  %j = add i32 %index, 4
  %a1 = getelementptr i32, ptr %a, i32 %j
  store i32 %value, ptr %a1, align 4
  %row = getelementptr [13 x i32], ptr %b, i32 %i
  %b0 = getelementptr i32, ptr %row, i32 %i
  store i32 %i, ptr %b0, align 4
  %b1 = getelementptr i32, ptr %row, i32 %j
  store i32 %i, ptr %b1, align 4
  %k = add i32 %index, 2
  %bm = getelementptr i32, ptr %row, i32 %k
  %old = load i32, ptr %bm, align 4
  %next = add i32 %old, 1
  store i32 %next, ptr %bm, align 4
  %reloaded = load i32, ptr %a0, align 4
  %later = add i32 %index, 12
  %lastrow = getelementptr [13 x i32], ptr %b, i32 %later
  %last = getelementptr i32, ptr %lastrow, i32 %i
  store i32 %reloaded, ptr %last, align 4
  ret void
}
