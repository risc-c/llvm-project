; REQUIRES: riscc-registered-target
; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=riscc-none-elf -mcpu=full %t/stack.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=STACK
; RUN: not --crash llc -mtriple=riscc-none-elf -mcpu=full %t/call.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL

; STACK: LLVM ERROR: RISC-C stack frame exceeds the 16-bit address space
; CALL: LLVM ERROR: RISC-C initial backend supports outgoing call frames of at most 126 bytes

;--- stack.ll
target triple = "riscc-none-elf"

define void @too_large_frame() {
entry:
  %large = alloca [65536 x i8], align 2
  call void asm sideeffect "", "r"(ptr %large)
  ret void
}

;--- call.ll
target triple = "riscc-none-elf"

declare void @sink(i1024)

define void @too_large_call_frame() {
entry:
  call void @sink(i1024 0)
  ret void
}
