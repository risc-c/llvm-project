; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -riscc-wide-mul-cost -verify-machineinstrs < %s | FileCheck %s \
; RUN:   --check-prefixes=FULL,RC16FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -riscc-wide-mul-cost -verify-machineinstrs < %s | FileCheck %s \
; RUN:   --check-prefixes=MIN,RC16MIN
; RUN: sed 's/p:16:16/p:32:32/' %s | llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -riscc-wide-mul-cost \
; RUN:   -verify-machineinstrs | FileCheck %s --check-prefixes=FULL,RC32FULL
; RUN: sed 's/p:16:16/p:32:32/' %s | llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -riscc-wide-mul-cost \
; RUN:   -verify-machineinstrs | FileCheck %s --check-prefixes=MIN,RC32MIN

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:32-n8:16-S16"
target triple = "riscc-none-elf"

; Full-width shifts are charged one instruction per native shift byte on Full
; and one instruction per bit on Min. These constants exercise binary Horner,
; power-of-two-minus-one, and negative forms.
define i16 @mul_17(i16 %x) {
; FULL-LABEL: mul_17:
; FULL:       slli r0, r1, 4
; FULL-NEXT:  add r1, r0, r1
; RC16MIN-LABEL: mul_17:
; RC16MIN:       ldi r2, 17
; RC16MIN:       ldi16 r0, __mulhi3
; RC16MIN:       jalr s7, r0
; RC32MIN-LABEL: mul_17:
; RC32MIN:       ldi r2, 17
; RC32MIN:       ldpc r0, {{.*}}
; RC32MIN:       jalr s7, r0
  %v = mul i16 %x, 17
  ret i16 %v
}

define i16 @mul_7(i16 %x) {
; FULL-LABEL: mul_7:
; FULL:       slli r0, r1, 3
; FULL-NEXT:  sub r1, r0, r1
; MIN-LABEL: mul_7:
; MIN:       add r0, r1, r1
; MIN-NEXT:  add r0, r0, r0
; MIN-NEXT:  add r0, r0, r0
; MIN-NEXT:  sub r1, r0, r1
  %v = mul i16 %x, 7
  ret i16 %v
}

define i16 @mul_24(i16 %x) {
; FULL-LABEL: mul_24:
; FULL:       slli r0, r1, 2
; FULL-NEXT:  sub r0, r0, r1
; FULL-NEXT:  slli r1, r0, 3
; RC16MIN-LABEL: mul_24:
; RC16MIN:       ldi r2, 24
; RC16MIN:       ldi16 r0, __mulhi3
; RC16MIN:       jalr s7, r0
; RC32MIN-LABEL: mul_24:
; RC32MIN:       ldi r2, 24
; RC32MIN:       ldpc r0, {{.*}}
; RC32MIN:       jalr s7, r0
  %v = mul i16 %x, 24
  ret i16 %v
}

define i16 @mul_neg7(i16 %x) {
; FULL-LABEL: mul_neg7:
; FULL:       slli r0, r1, 3
; FULL-NEXT:  sub r1, r1, r0
; MIN-LABEL: mul_neg7:
; MIN:       add r0, r1, r1
; MIN-NEXT:  add r0, r0, r0
; MIN-NEXT:  add r0, r0, r0
; MIN-NEXT:  sub r1, r1, r0
  %v = mul i16 %x, -7
  ret i16 %v
}

define i16 @mul_neg17(i16 %x) {
; RC16FULL-LABEL: mul_neg17:
; RC16FULL:       ldi16 r0, 65519
; RC16FULL-NEXT:  mul r1, r1, r0
; RC32FULL-LABEL: mul_neg17:
; RC32FULL:       ldpc r0, {{.*}}
; RC32FULL-NEXT:  mul r1, r1, r0
; RC16MIN-LABEL: mul_neg17:
; RC16MIN:       ldi16 r2, 65519
; RC16MIN:       ldi16 r0, __mulhi3
; RC16MIN:       jalr s7, r0
; RC32MIN-LABEL: mul_neg17:
; RC32MIN:       ldpc r2, {{.*}}
; RC32MIN:       ldpc r0, {{.*}}
; RC32MIN:       jalr s7, r0
  %v = mul i16 %x, -17
  ret i16 %v
}

define i16 @mul_17_size(i16 %x) optsize {
; FULL-LABEL: mul_17_size:
; FULL:       slli r0, r1, 4
; FULL-NEXT:  add r1, r0, r1
; RC16MIN-LABEL: mul_17_size:
; RC16MIN:       ldi r2, 17
; RC16MIN:       ldi16 r0, __mulhi3
; RC16MIN:       jalr s7, r0
; RC32MIN-LABEL: mul_17_size:
; RC32MIN:       ldi r2, 17
; RC32MIN:       ldpc r0, {{.*}}
; RC32MIN:       jalr s7, r0
  %v = mul i16 %x, 17
  ret i16 %v
}

define i16 @mul_24_size(i16 %x) optsize {
; FULL-LABEL: mul_24_size:
; FULL:       ldi r0, 24
; FULL-NEXT:  mul r1, r1, r0
; RC16MIN-LABEL: mul_24_size:
; RC16MIN:       ldi r2, 24
; RC16MIN:       ldi16 r0, __mulhi3
; RC16MIN:       jalr s7, r0
; RC32MIN-LABEL: mul_24_size:
; RC32MIN:       ldi r2, 24
; RC32MIN:       ldpc r0, {{.*}}
; RC32MIN:       jalr s7, r0
  %v = mul i16 %x, 24
  ret i16 %v
}

; The RC32 target must apply the same native-shift costing to native i32
; values, rather than only exercising the i16 legalization path above.
define i32 @mul_17_i32(i32 %x) {
; RC32FULL-LABEL: mul_17_i32:
; RC32FULL:       slli r0, r1, 4
; RC32FULL-NEXT:  add r1, r0, r1
; RC32MIN-LABEL: mul_17_i32:
; RC32MIN:       ldi r2, 17
; RC32MIN:       ldpc r0, {{.*}}
; RC32MIN:       jalr s7, r0
  %v = mul i32 %x, 17
  ret i32 %v
}

define i32 @mul_7_i32(i32 %x) {
; RC32FULL-LABEL: mul_7_i32:
; RC32FULL:       slli r0, r1, 3
; RC32FULL-NEXT:  sub r1, r0, r1
; RC32MIN-LABEL: mul_7_i32:
; RC32MIN:       add r0, r1, r1
; RC32MIN-NEXT:  add r0, r0, r0
; RC32MIN-NEXT:  add r0, r0, r0
; RC32MIN-NEXT:  sub r1, r0, r1
  %v = mul i32 %x, 7
  ret i32 %v
}

define i32 @mul_24_i32(i32 %x) {
; RC32FULL-LABEL: mul_24_i32:
; RC32FULL:       slli r0, r1, 2
; RC32FULL-NEXT:  sub r0, r0, r1
; RC32FULL-NEXT:  slli r1, r0, 3
; RC32MIN-LABEL: mul_24_i32:
; RC32MIN:       ldi r2, 24
; RC32MIN:       ldpc r0, {{.*}}
; RC32MIN:       jalr s7, r0
  %v = mul i32 %x, 24
  ret i32 %v
}

define i32 @mul_neg7_i32(i32 %x) {
; RC32FULL-LABEL: mul_neg7_i32:
; RC32FULL:       slli r0, r1, 3
; RC32FULL-NEXT:  sub r1, r1, r0
; RC32MIN-LABEL: mul_neg7_i32:
; RC32MIN:       add r0, r1, r1
; RC32MIN-NEXT:  add r0, r0, r0
; RC32MIN-NEXT:  add r0, r0, r0
; RC32MIN-NEXT:  sub r1, r1, r0
  %v = mul i32 %x, -7
  ret i32 %v
}

define i32 @mul_neg17_i32(i32 %x) {
; RC32FULL-LABEL: mul_neg17_i32:
; RC32FULL:       ldpc r0, {{.*}}
; RC32FULL-NEXT:  mul r1, r1, r0
; RC32MIN-LABEL: mul_neg17_i32:
; RC32MIN:       ldpc r2, {{.*}}
; RC32MIN:       ldpc r0, {{.*}}
; RC32MIN:       jalr s7, r0
  %v = mul i32 %x, -17
  ret i32 %v
}

define i32 @mul_17_i32_size(i32 %x) optsize {
; RC32FULL-LABEL: mul_17_i32_size:
; RC32FULL:       slli r0, r1, 4
; RC32FULL-NEXT:  add r1, r0, r1
; RC32MIN-LABEL: mul_17_i32_size:
; RC32MIN:       ldi r2, 17
; RC32MIN:       ldpc r0, {{.*}}
; RC32MIN:       jalr s7, r0
  %v = mul i32 %x, 17
  ret i32 %v
}

define i32 @mul_24_i32_size(i32 %x) optsize {
; RC32FULL-LABEL: mul_24_i32_size:
; RC32FULL:       ldi r0, 24
; RC32FULL-NEXT:  mul r1, r1, r0
; RC32MIN-LABEL: mul_24_i32_size:
; RC32MIN:       ldi r2, 24
; RC32MIN:       ldpc r0, {{.*}}
; RC32MIN:       jalr s7, r0
  %v = mul i32 %x, 24
  ret i32 %v
}
