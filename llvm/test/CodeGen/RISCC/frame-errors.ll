; REQUIRES: riscc-registered-target
; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=riscc-none-elf -mcpu=full %t/stack.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=STACK
; RUN: not --crash llc -mtriple=riscc-none-elf -mcpu=full %t/realign.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=REALIGN
; RUN: not --crash llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 %t/realign.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=REALIGN

; STACK: LLVM ERROR: RISC-C stack frame exceeds the 16-bit address space
; REALIGN: LLVM ERROR: RISC-C does not support stack realignment

;--- stack.ll
target triple = "riscc-none-elf"

define void @too_large_frame() {
entry:
  %large = alloca [65536 x i8], align 2
  call void asm sideeffect "", "r"(ptr %large)
  ret void
}

;--- realign.ll
define void @aligned_frame() {
  %value = alloca i16, align 16
  call void asm sideeffect "", "r"(ptr %value)
  ret void
}
