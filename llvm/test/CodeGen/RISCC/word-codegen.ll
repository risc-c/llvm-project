; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"
target triple = "riscc-none-elf"

declare i32 @callee(i32)

define i32 @decrement_word(i32 %value) {
; CHECK-LABEL: decrement_word:
; CHECK-NOT:   ldpc
; CHECK:       addi r1, -1
; CHECK-NOT:   ldpc
; CHECK:       ret s7
  %next = add i32 %value, -1
  ret i32 %next
}

; The scaled address is shared by neighboring native-word accesses.  The
; element displacements remain byte offsets from that one base.
define i32 @word_neighbors(ptr %base, i32 %index, i32 %value) {
; CHECK-LABEL: word_neighbors:
; CHECK:       slli [[SCALED:r[0-7]]], r2, 2
; CHECK:       add [[BASE:r[0-7]]], r1, [[SCALED]]
; CHECK-DAG:   ld [[LEFT:r[0-7]]], {{\[}}[[BASE]] + -4{{\]}}
; CHECK-DAG:   st r3, {{\[}}[[BASE]] + 0{{\]}}
; CHECK-DAG:   ld [[RIGHT:r[0-7]]], {{\[}}[[BASE]] + 4{{\]}}
; CHECK:       add {{r[0-7]}}, [[LEFT]], [[RIGHT]]
; CHECK:       ret s7
  %at = getelementptr i32, ptr %base, i32 %index
  %left = getelementptr i32, ptr %at, i32 -1
  %right = getelementptr i32, ptr %at, i32 1
  %a = load i32, ptr %left, align 4
  store i32 %value, ptr %at, align 4
  %b = load i32, ptr %right, align 4
  %sum = add i32 %a, %b
  ret i32 %sum
}

define i32 @select_eq_small(i32 %value, i32 %when_true, i32 %when_false) {
; CHECK-LABEL: select_eq_small:
; CHECK-NOT:   ldpc
; CHECK-NOT:   ldi {{r[0-7]}}, 7
; CHECK:       cmpi r1, 7
; CHECK-NOT:   ldpc
; CHECK-NOT:   ldi {{r[0-7]}}, 7
; CHECK:       ret s7
  %equal = icmp eq i32 %value, 7
  %result = select i1 %equal, i32 %when_true, i32 %when_false
  ret i32 %result
}

; The call block branches to a return block in the IR.  It must still lower
; the direct call as a sibling tail call, with the shared return block kept
; separate for the other predecessor.
define i32 @direct_tail_shared_return(i1 %condition, i32 %value) {
entry:
  br i1 %condition, label %call, label %return

call:
  %result = tail call i32 @callee(i32 %value)
  br label %return

return:
  %joined = phi i32 [%result, %call], [%value, %entry]
  ret i32 %joined
}
; CHECK-LABEL: direct_tail_shared_return:
; CHECK:       beqz [[RETURN:.LBB[0-9_]+]]
; CHECK:       {{(jall s0, callee|jalr s0, r0)}}
; CHECK-NOT:   ret
; CHECK:       [[RETURN]]:
; CHECK-NEXT:  ret s7

; Keep the signed i8 PHI in a native register.  The signed comparison and
; call consume the same widened value, so only one sign extension belongs in
; the loop body.
define i32 @signed_byte_phi_loop(i8 %start) {
entry:
  br label %loop

loop:
  %index = phi i8 [%start, %entry], [%next, %body]
  %wide = sext i8 %index to i32
  %in_range = icmp slt i32 %wide, 10
  br i1 %in_range, label %body, label %exit

body:
  %called = call i32 @callee(i32 %wide)
  %next = add i8 %index, 1
  br label %loop

exit:
  ret i32 0
}
; CHECK-LABEL: signed_byte_phi_loop:
; CHECK:       [[LOOP:.LBB[0-9_]+]]:
; CHECK:       andi [[BYTE:r[0-7]]], 255
; CHECK-NEXT:  xori [[BYTE]], 128
; CHECK-NEXT:  addi [[BYTE]], -128
; CHECK-NOT:   andi
; CHECK:       slt {{r[0-7]}}, {{r[0-7]}}, [[BYTE]]
; CHECK-NOT:   andi
; CHECK:       jalr s7,
; CHECK-NOT:   andi
; CHECK:       addi {{r[0-7]}}, 1
; CHECK:       jmp8 [[LOOP]]
