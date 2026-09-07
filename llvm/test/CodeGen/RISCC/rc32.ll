; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -filetype=obj %s -o %t
; RUN: llvm-readobj -r %t | FileCheck %s --check-prefix=RELOC
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s --check-prefix=FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32,+mdu -verify-machineinstrs < %s | FileCheck %s --check-prefix=MDU

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"
target triple = "riscc-none-elf"

@word = external global i32, align 4
@half = external global i16, align 2

declare i32 @callee(i32)
declare void @wide_callee(i1024)

define void @literal_before_long_body() {
; CHECK-LABEL: literal_before_long_body:
; CHECK:       ldpc r0, [[EARLY_CALLEE:.Ltmp[0-9]+]]
; CHECK:       [[EARLY_CALLEE]]:
; CHECK-NEXT:  .long call_target(callee)
; CHECK:       .zero 300
  %call = call i32 @callee(i32 0)
  call void asm sideeffect ".space 300", ""()
  ret void
}

define i32 @literal_call(i32 %x) {
; CHECK-LABEL: literal_call:
; CHECK:       ldpc r0, [[CALLEE:.Ltmp[0-9]+]]
; CHECK-NEXT:  jalr s7, r0
; CHECK:       addi r1, 1
; CHECK:       ret s7
; CHECK:       [[CALLEE]]:
; CHECK-NEXT:  .long call_target(callee)
  %call = call i32 @callee(i32 %x)
  %result = add i32 %call, 1
  ret i32 %result
}

define i32 @memory(i32 %value) {
; CHECK-LABEL: memory:
; CHECK:       ldpc r0, [[WORD:.Ltmp[0-9]+]]
; CHECK:       ld {{r[0-7]}}, [r{{[0-7]}} + 0]
; CHECK:       st r1, [r{{[0-7]}} + 0]
; CHECK:       ldpc r0, [[HALF:.Ltmp[0-9]+]]
; CHECK:       ldhs {{r[0-7]}}, [r{{[0-7]}}]
; CHECK:       ret s7
; CHECK:       [[WORD]]:
; CHECK-NEXT:  .long word
; CHECK-NEXT:  [[HALF]]:
; CHECK-NEXT:  .long half
  %old = load i32, ptr @word, align 4
  store i32 %value, ptr @word, align 4
  %short = load i16, ptr @half, align 2
  %extended = sext i16 %short to i32
  %result = add i32 %old, %extended
  ret i32 %result
}

@byte = external global i8, align 1
@half_unsigned = external global i16, align 2

define i32 @typed_memory(i8 %byte_value, i16 %half_value) {
; CHECK-LABEL: typed_memory:
; CHECK:       ldb {{r[0-7]}}, [r{{[0-7]}}]
; CHECK:       stb {{r[0-7]}}, [r{{[0-7]}}]
; CHECK:       ldh {{r[0-7]}}, [r{{[0-7]}}]
; CHECK:       sth {{r[0-7]}}, [r{{[0-7]}}]
  %old_byte = load i8, ptr @byte, align 1
  %old_half = load i16, ptr @half_unsigned, align 2
  store i8 %byte_value, ptr @byte, align 1
  store i16 %half_value, ptr @half_unsigned, align 2
  %wide_byte = zext i8 %old_byte to i32
  %wide_half = zext i16 %old_half to i32
  %result = add i32 %wide_byte, %wide_half
  ret i32 %result
}

define i32 @signed_min(i32 %x, i32 %y) {
; CHECK-LABEL: signed_min:
; CHECK:       slt r0, r1, r2
; CHECK:       beqz
  %less = icmp slt i32 %x, %y
  %result = select i1 %less, i32 %x, i32 %y
  ret i32 %result
}

define i32 @unsigned_min(i32 %x, i32 %y) {
; CHECK-LABEL: unsigned_min:
; CHECK:       sltu r0, r1, r2
; CHECK:       beqz
  %less = icmp ult i32 %x, %y
  %result = select i1 %less, i32 %x, i32 %y
  ret i32 %result
}

define i32 @four_arguments(i32 %a, i32 %b, i32 %c, i32 %d) {
; CHECK-LABEL: four_arguments:
; CHECK:       ld {{r[0-7]}}, [r7 + 4]
  %ab = add i32 %a, %b
  %cd = add i32 %c, %d
  %result = add i32 %ab, %cd
  ret i32 %result
}

define i32 @constant_shl_19(i32 %value) minsize {
; CHECK-LABEL: constant_shl_19:
; CHECK:       jalr s7, r0
; CHECK:       .long call_target(__riscc_shlsi19)
; FULL-LABEL:  constant_shl_19:
; FULL:        slli [[SHL:r[0-7]]], r1, 8
; FULL-NEXT:   slli [[SHL]], [[SHL]], 8
; FULL-NEXT:   slli r1, [[SHL]], 3
  %result = shl i32 %value, 19
  ret i32 %result
}

define i32 @constant_lshr_19(i32 %value) minsize {
; CHECK-LABEL: constant_lshr_19:
; CHECK:       jalr s7, r0
; CHECK:       .long call_target(__riscc_lshrsi19)
; FULL-LABEL:  constant_lshr_19:
; FULL:        srli [[LSHR:r[0-7]]], r1, 8
; FULL-NEXT:   srli [[LSHR]], [[LSHR]], 8
; FULL-NEXT:   srli r1, [[LSHR]], 3
  %result = lshr i32 %value, 19
  ret i32 %result
}

define i32 @constant_ashr_19(i32 %value) minsize {
; CHECK-LABEL: constant_ashr_19:
; CHECK:       jalr s7, r0
; CHECK:       .long call_target(__riscc_ashrsi19)
; FULL-LABEL:  constant_ashr_19:
; FULL:        srai [[ASHR:r[0-7]]], r1, 8
; FULL-NEXT:   srai [[ASHR]], [[ASHR]], 8
; FULL-NEXT:   srai r1, [[ASHR]], 3
  %result = ashr i32 %value, 19
  ret i32 %result
}

define i32 @native_mul(i32 %left, i32 %right) {
; CHECK-LABEL: native_mul:
; CHECK:       jalr s7, r0
; CHECK:       .long call_target(__mulsi3)
; FULL-LABEL:  native_mul:
; FULL:        mul r1, r1, r2
  %result = mul i32 %left, %right
  ret i32 %result
}

define i32 @native_udiv(i32 %left, i32 %right) {
; MDU-LABEL: native_udiv:
; MDU:       ldi r0, 0
; MDU-NEXT:  divu r0, r1, r2
  %result = udiv i32 %left, %right
  ret i32 %result
}

define i32 @native_mulhu(i32 %left, i32 %right) {
; MDU-LABEL: native_mulhu:
; MDU:       mulhu r0, r1, r2
  %left64 = zext i32 %left to i64
  %right64 = zext i32 %right to i64
  %product = mul i64 %left64, %right64
  %high64 = lshr i64 %product, 32
  %high = trunc i64 %high64 to i32
  ret i32 %high
}

define void @large_call_frame() {
; CHECK-LABEL: large_call_frame:
; CHECK:       addi r7, -128
; CHECK-NEXT:  addi r7, -8
; CHECK:       st r0, [r7 + 124]
; CHECK:       ldpc r0, {{.Ltmp[0-9]+}}
; CHECK-NEXT:  jalr s7, r0
  call void @wide_callee(i1024 0)
  ret void
}

define i32 @literal_before_jump(i32 %a, i32 %b) {
; CHECK-LABEL: literal_before_jump:
; CHECK:       .zero 300
; CHECK:       ldpc {{r[0-7]}}, [[WORD:.Ltmp[0-9]+]]
; CHECK:       jalr s0, r0
; CHECK:       [[WORD]]:
; CHECK-NEXT:  .long word
; CHECK:       .zero 300
entry:
  call void asm sideeffect ".space 300", ""()
  %value = load i32, ptr @word, align 4
  %same = icmp eq i32 %a, %b
  br i1 %same, label %far, label %near, !prof !0

near:
  call void asm sideeffect ".space 300", ""()
  ret i32 %value

far:
  ret i32 0
}

!0 = !{!"branch_weights", i32 1, i32 1000}

; RELOC: Relocations [
; RELOC-NOT: R_RISCC_HI8
; RELOC-NOT: R_RISCC_LO8
; RELOC: R_RISCC_ABS32 .text
; RELOC-NOT: R_RISCC_HI8
; RELOC-NOT: R_RISCC_LO8
