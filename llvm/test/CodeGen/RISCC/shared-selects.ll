; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s \
; RUN:   --check-prefix=RC32
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -O2 -verify-machineinstrs < %s -o /dev/null
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -O2 -verify-machineinstrs < %s -o /dev/null
; RUN: opt -passes=debugify -S < %s | llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs -o /dev/null
; RUN: opt -passes=debugify -S < %s | llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs \
; RUN:   -o /dev/null

; Each pair shares one comparison/branch, including when an independent add
; is scheduled between the selects. Keep their results distinct and live.
define {i16, i16} @pair16(i16 %a, i16 %b, i16 %c, i16 %d) {
; RC16-LABEL: pair16:
; RC16: sltu
; RC16: b{{eqz|nez}}
; RC16-NOT: sltu
; RC16-NOT: b{{eqz|nez}}
; RC16: ret
  %cond = icmp ult i16 %a, %b
  %first = select i1 %cond, i16 %c, i16 %d
  %plus = add i16 %c, 3
  %second = select i1 %cond, i16 %plus, i16 %a
  %r0 = insertvalue {i16, i16} poison, i16 %first, 0
  %r1 = insertvalue {i16, i16} %r0, i16 %second, 1
  ret {i16, i16} %r1
}

define {i32, i32} @pair32(i32 %a, i32 %b, i32 %c, i32 %d) {
; RC32-LABEL: pair32:
; RC32: sltu
; RC32: b{{eqz|nez}}
; RC32-NOT: sltu
; RC32-NOT: b{{eqz|nez}}
; RC32: ret
  %cond = icmp ult i32 %a, %b
  %first = select i1 %cond, i32 %c, i32 %d
  %plus = add i32 %c, 3
  %second = select i1 %cond, i32 %plus, i32 %a
  %r0 = insertvalue {i32, i32} poison, i32 %first, 0
  %r1 = insertvalue {i32, i32} %r0, i32 %second, 1
  ret {i32, i32} %r1
}

define {i16, i16} @immediate16(i16 %a, i16 %b, i16 %c) {
; RC16-LABEL: immediate16:
; RC16: cmpi
; RC16: b{{eqz|nez}}
; RC16-NOT: cmpi
; RC16-NOT: b{{eqz|nez}}
; RC16: ret
  %cond = icmp eq i16 %a, 7
  %first = select i1 %cond, i16 %b, i16 %c
  %second = select i1 %cond, i16 %c, i16 %a
  %r0 = insertvalue {i16, i16} poison, i16 %first, 0
  %r1 = insertvalue {i16, i16} %r0, i16 %second, 1
  ret {i16, i16} %r1
}

define {i32, i32} @immediate32(i32 %a, i32 %b, i32 %c) {
; RC32-LABEL: immediate32:
; RC32: cmpi
; RC32: b{{eqz|nez}}
; RC32-NOT: cmpi
; RC32-NOT: b{{eqz|nez}}
; RC32: ret
  %cond = icmp eq i32 %a, 7
  %first = select i1 %cond, i32 %b, i32 %c
  %second = select i1 %cond, i32 %c, i32 %a
  %r0 = insertvalue {i32, i32} poison, i32 %first, 0
  %r1 = insertvalue {i32, i32} %r0, i32 %second, 1
  ret {i32, i32} %r1
}

; Do not move a use of the first selection into the head of its diamond.
define i32 @dependent(i32 %a, i32 %b, i32 %c, i32 %d) {
  %cond = icmp ult i32 %a, %b
  %first = select i1 %cond, i32 %c, i32 %d
  %twice = add i32 %first, %a
  %second = select i1 %cond, i32 %twice, i32 %c
  %result = xor i32 %first, %second
  ret i32 %result
}

; Memory and calls remain barriers even if the comparisons match.
declare i32 @consume(i32)
define i32 @barriers(i32 %a, i32 %b, ptr %p) {
  %cond = icmp ult i32 %a, %b
  %first = select i1 %cond, i32 %a, i32 %b
  store volatile i32 %first, ptr %p
  %value = call i32 @consume(i32 %first)
  %second = select i1 %cond, i32 %value, i32 %a
  ret i32 %second
}
