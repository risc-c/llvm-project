; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=COMMON,NONNANO
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=COMMON,NONNANO
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=COMMON,NANO

target datalayout = "e-m:e-P1-p:16:16-p1:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

@byte = global i8 0, align 1

define i16 @load_byte_direct(ptr %address) {
; COMMON-LABEL: load_byte_direct:
; COMMON:       ldb r1, [r1]
; COMMON-NEXT:  {{rets|ret r6}}
  %value = load i8, ptr %address, align 1
  %extended = zext i8 %value to i16
  ret i16 %extended
}

define i16 @load_byte_index(ptr %base, i16 %index) {
; COMMON-LABEL: load_byte_index:
; COMMON:       add [[ADDRESS:r[0-6]]], r1, r2
; COMMON-NEXT:  ldb r1, {{\[}}[[ADDRESS]]{{\]}}
; COMMON-NEXT:  {{rets|ret r6}}
  %address = getelementptr i8, ptr %base, i16 %index
  %value = load i8, ptr %address, align 1
  %extended = zext i8 %value to i16
  ret i16 %extended
}

define i16 @load_byte_offset(ptr %base) {
; COMMON-LABEL: load_byte_offset:
; COMMON:       addi r1, 5
; COMMON-NEXT:  ldb r1, [r1]
; COMMON-NEXT:  {{rets|ret r6}}
  %address = getelementptr i8, ptr %base, i16 5
  %value = load i8, ptr %address, align 1
  %extended = zext i8 %value to i16
  ret i16 %extended
}

define i16 @load_byte_global() {
; COMMON-LABEL: load_byte_global:
; COMMON:       li [[ADDRESS:r[0-6]]], byte
; COMMON-NEXT:  ldb r1, {{\[}}[[ADDRESS]]{{\]}}
; COMMON-NEXT:  {{rets|ret r6}}
  %value = load i8, ptr @byte, align 1
  %extended = zext i8 %value to i16
  ret i16 %extended
}

define i16 @load_signed_byte_direct(ptr %address) {
; COMMON-LABEL: load_signed_byte_direct:
; NONNANO:      ldbs r1, [r1]
; NONNANO-NEXT: rets
; NANO:         ldb r1, [r1]
; NANO-NEXT:    xori r1, 128
; NANO-NEXT:    addi r1, -128
; NANO-NEXT:    ret r6
  %value = load i8, ptr %address, align 1
  %extended = sext i8 %value to i16
  ret i16 %extended
}

define i16 @load_signed_byte_index(ptr %base, i16 %index) {
; COMMON-LABEL: load_signed_byte_index:
; COMMON:       add [[ADDRESS:r[0-6]]], r1, r2
; NONNANO-NEXT: ldbs r1, {{\[}}[[ADDRESS]]{{\]}}
; NONNANO-NEXT: rets
; NANO-NEXT:    ldb r1, {{\[}}[[ADDRESS]]{{\]}}
; NANO-NEXT:    xori r1, 128
; NANO-NEXT:    addi r1, -128
; NANO-NEXT:    ret r6
  %address = getelementptr i8, ptr %base, i16 %index
  %value = load i8, ptr %address, align 1
  %extended = sext i8 %value to i16
  ret i16 %extended
}
