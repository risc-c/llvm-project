; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=COMMON,FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=COMMON,MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=COMMON,NANO

target datalayout = "e-m:e-P1-p:16:16-p1:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

define i16 @constant_5() {
; COMMON-LABEL: constant_5:
; COMMON:       ldi r1, 5
; COMMON-NEXT:  {{rets|ret r6}}
  ret i16 5
}

define i16 @constant_ff00() {
; COMMON-LABEL: constant_ff00:
; COMMON:       lui r1, 255
; COMMON-NEXT:  {{rets|ret r6}}
  ret i16 -256
}

define i16 @constant_1234() {
; COMMON-LABEL: constant_1234:
; COMMON:       li r1, 4660
; COMMON-NEXT:  {{rets|ret r6}}
  ret i16 4660
}

define i16 @signed_less(i16 %a, i16 %b) {
; COMMON-LABEL: signed_less:
; FULL:         slt r1, r1, r2
; MIN:          slt r1, r1, r2
; NANO:         lui
; NANO:         xor
; NANO:         xor
; NANO:         sltu
; COMMON-NOT:   jmp
; COMMON-NOT:   ldi {{.*}}, 1
  %cmp = icmp slt i16 %a, %b
  %result = zext i1 %cmp to i16
  ret i16 %result
}

define i16 @unsigned_at_least(i16 %a, i16 %b) {
; COMMON-LABEL: unsigned_at_least:
; COMMON:       sltu
; COMMON-NEXT:  xori {{.*}}, 1
; COMMON-NOT:   jmp
  %cmp = icmp uge i16 %a, %b
  %result = zext i1 %cmp to i16
  ret i16 %result
}

define i16 @branch_equal_5(i16 %value) {
; FULL-LABEL: branch_equal_5:
; FULL:       cmpi r1, 5
; FULL-NEXT:  b{{eq|ne}}z
; MIN-LABEL: branch_equal_5:
; MIN:       cmpi r1, 5
; MIN-NEXT:  b{{eq|ne}}z
; NANO-LABEL: branch_equal_5:
; NANO-NOT:   cmpi
  %cmp = icmp eq i16 %value, 5
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 11

no:
  ret i16 22
}

define i16 @branch_equal_minus_one(i16 %value) {
; FULL-LABEL: branch_equal_minus_one:
; FULL:       cmpi r1, -1
; FULL-NEXT:  b{{eq|ne}}z
; MIN-LABEL: branch_equal_minus_one:
; MIN:       cmpi r1, -1
; MIN-NEXT:  b{{eq|ne}}z
; NANO-LABEL: branch_equal_minus_one:
; NANO-NOT:   cmpi
  %cmp = icmp eq i16 %value, -1
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 11

no:
  ret i16 22
}

define i16 @branch_negative(i16 %value) {
; COMMON-LABEL: branch_negative:
; COMMON:       mov r0, r1
; COMMON-NEXT:  bltz
  %cmp = icmp slt i16 %value, 0
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 11

no:
  ret i16 22
}

define i16 @load_word_index(ptr %base, i16 %index) {
; COMMON-LABEL: load_word_index:
; COMMON:       ldwx
; COMMON-NOT:   add
  %address = getelementptr i16, ptr %base, i16 %index
  %value = load i16, ptr %address, align 2
  ret i16 %value
}

define i16 @load_byte_index(ptr %base, i16 %index) {
; COMMON-LABEL: load_byte_index:
; COMMON-NOT:   add
; COMMON:       ldb
  %address = getelementptr i8, ptr %base, i16 %index
  %value = load i8, ptr %address
  %result = zext i8 %value to i16
  ret i16 %result
}

define i16 @load_signed_byte(ptr %address) {
; NANO-LABEL: load_signed_byte:
; NANO:       ldb
; NANO-NOT:   andi
; NANO:       xori {{.*}}, 128
; NANO-NEXT:  addi {{.*}}, -128
  %value = load i8, ptr %address
  %result = sext i8 %value to i16
  ret i16 %result
}
