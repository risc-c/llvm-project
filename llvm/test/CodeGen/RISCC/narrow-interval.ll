; REQUIRES: riscc-registered-target
; RUN: opt -verify-each -passes=riscc-prepare -S %s | FileCheck %s --check-prefixes=IR,RC16
; RUN: sed 's/p:16:16/p:32:32/' %s | opt -verify-each -passes=riscc-prepare -S - | FileCheck %s --check-prefixes=IR,RC32
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscc -mcpu=min -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-n8:16-S16"
target triple = "riscc-none-elf"

; The complement of [48, 58) wraps in i8. Widen before subtracting 48,
; so native unsigned underflow handles values below the lower endpoint.
define i16 @byte_outside(ptr %p) {
; IR-LABEL: define i16 @byte_outside
; IR:       zext i8 %x to i{{16|32}}
; IR-NEXT:  add i{{16|32}} {{.*}}, -48
; IR-NEXT:  icmp uge i{{16|32}} {{.*}}, 10
; ASM-LABEL: byte_outside:
; ASM:       ldb
; ASM-NOT:   andi
; ASM-NOT:   srli
; ASM:       addi {{r[0-7]}}, -48
; ASM-NOT:   andi
; ASM-NOT:   srli
; ASM:       sltu
  %x = load i8, ptr %p
  %biased = add i8 %x, -58
  %outside = icmp ult i8 %biased, -10
  br i1 %outside, label %yes, label %no
yes:
  ret i16 7
no:
  ret i16 3
}

define i1 @byte_inside(i8 %x) {
; IR-LABEL: define i1 @byte_inside
; IR:       zext i8 %x to i{{16|32}}
; IR-NEXT:  add i{{16|32}} {{.*}}, -48
; IR-NEXT:  icmp ult i{{16|32}} {{.*}}, 10
  %biased = add i8 %x, -58
  %inside = icmp uge i8 %biased, -10
  ret i1 %inside
}

; Inclusive endpoints are converted to a half-open interval as well.
define i1 @inclusive(i8 %x) {
; IR-LABEL: define i1 @inclusive
; IR:       add i{{16|32}} {{.*}}, -65
; IR-NEXT:  icmp ult i{{16|32}} {{.*}}, 26
  %biased = add i8 %x, -65
  %inside = icmp ule i8 %biased, 25
  ret i1 %inside
}

define i1 @exclusive(i8 %x) {
; IR-LABEL: define i1 @exclusive
; IR:       add i{{16|32}} {{.*}}, -65
; IR-NEXT:  icmp uge i{{16|32}} {{.*}}, 26
  %biased = add i8 %x, -65
  %outside = icmp ugt i8 %biased, 25
  ret i1 %outside
}

; A native-width comparison also benefits from the cheaper interval bound.
define i1 @halfword(i16 %x) {
; IR-LABEL: define i1 @halfword
; RC16:     add i16 %x, -48
; RC16-NEXT: icmp ult i16 {{.*}}, 10
; RC32:     zext i16 %x to i32
; RC32-NEXT: add i32 {{.*}}, -48
; RC32-NEXT: icmp ult i32 {{.*}}, 10
  %biased = add i16 %x, -58
  %inside = icmp uge i16 %biased, -10
  ret i1 %inside
}

; Do not duplicate arithmetic used elsewhere or materialize expensive bounds.
define i1 @shared(i8 %x, ptr %out) {
; IR-LABEL: define i1 @shared
; IR:       %biased = add i8 %x, -58
; IR:       %outside = icmp ult i8 %biased, -10
  %biased = add i8 %x, -58
  store i8 %biased, ptr %out
  %outside = icmp ult i8 %biased, -10
  ret i1 %outside
}

define i1 @expensive_lower(i8 %x) {
; IR-LABEL: define i1 @expensive_lower
; IR:       %biased = add i8 %x, 56
; IR:       %inside = icmp ult i8 %biased, 20
  %biased = add i8 %x, 56
  %inside = icmp ult i8 %biased, 20
  ret i1 %inside
}

define i1 @full_range(i8 %x) {
; IR-LABEL: define i1 @full_range
; IR:       %biased = add i8 %x, -58
; IR:       %inside = icmp ule i8 %biased, -1
  %biased = add i8 %x, -58
  %inside = icmp ule i8 %biased, -1
  ret i1 %inside
}

; The exclusive upper bound 0 represents the end of the narrow domain.
define i1 @top_interval(i8 %x) {
; IR-LABEL: define i1 @top_interval
; IR:       add i{{16|32}} {{.*}}, -120
; IR-NEXT:  icmp ult i{{16|32}} {{.*}}, 136
  %biased = add i8 %x, -120
  %inside = icmp ult i8 %biased, 136
  ret i1 %inside
}

; A zero lower endpoint still uses the native unsigned comparison.
define i1 @bottom_interval(i8 %x) {
; IR-LABEL: define i1 @bottom_interval
; IR:       zext i8 %x to i{{16|32}}
; IR-NEXT:  add i{{16|32}} {{.*}}, 0
; IR-NEXT:  icmp ult i{{16|32}} {{.*}}, 10
  %biased = add i8 %x, -10
  %inside = icmp uge i8 %biased, 246
  ret i1 %inside
}

define i1 @empty_range(i8 %x) {
; IR-LABEL: define i1 @empty_range
; IR:       %biased = add i8 %x, -58
; IR:       %inside = icmp ult i8 %biased, 0
  %biased = add i8 %x, -58
  %inside = icmp ult i8 %biased, 0
  ret i1 %inside
}
