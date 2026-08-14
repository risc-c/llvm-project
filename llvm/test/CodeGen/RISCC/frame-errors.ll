; REQUIRES: riscc-registered-target
; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=riscc-none-elf -mcpu=full %t/stack.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=STACK

; STACK: LLVM ERROR: RISC-C stack frame exceeds the 16-bit address space

;--- stack.ll
target triple = "riscc-none-elf"

define void @too_large_frame() {
entry:
  %large = alloca [65536 x i8], align 2
  call void asm sideeffect "", "r"(ptr %large)
  ret void
}
