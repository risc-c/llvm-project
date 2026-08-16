; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -filetype=obj %s -o %t
; RUN: llvm-readobj -r %t | FileCheck %s --check-prefix=RELOC

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"
target triple = "riscc-none-elf"

@word = external global i32, align 4
@half = external global i16, align 2

declare i32 @callee(i32)
declare void @wide_callee(i1024)

define void @literal_before_long_body() {
; CHECK:       [[EARLY_CALLEE:.Ltmp[0-9]+]]:
; CHECK-NEXT:  .long callee
; CHECK-LABEL: literal_before_long_body:
; CHECK:       ldpc r0, [[EARLY_CALLEE]]
; CHECK:       .zero 300
  %call = call i32 @callee(i32 0)
  call void asm sideeffect ".space 300", ""()
  ret void
}

define i32 @literal_call(i32 %x) {
; CHECK:       [[CALLEE:.Ltmp[0-9]+]]:
; CHECK-NEXT:  .long callee
; CHECK-LABEL: literal_call:
; CHECK:       ldpc r0, [[CALLEE]]
; CHECK-NEXT:  jalr s7, r0
; CHECK:       addi r1, 1
; CHECK:       ret s7
  %call = call i32 @callee(i32 %x)
  %result = add i32 %call, 1
  ret i32 %result
}

define i32 @memory(i32 %value) {
; CHECK:       [[WORD:.Ltmp[0-9]+]]:
; CHECK-NEXT:  .long word
; CHECK-NEXT:  [[HALF:.Ltmp[0-9]+]]:
; CHECK-NEXT:  .long half
; CHECK-LABEL: memory:
; CHECK:       ldpc r0, [[WORD]]
; CHECK:       ld {{r[0-7]}}, [r{{[0-7]}} + 0]
; CHECK:       st r1, [r{{[0-7]}} + 0]
; CHECK:       ldpc r0, [[HALF]]
; CHECK:       ldhs {{r[0-7]}}, [r{{[0-7]}}]
; CHECK:       ret s7
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
