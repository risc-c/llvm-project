; REQUIRES: riscc-registered-target
; RUN: split-file %s %t
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %t/rc16.ll -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %t/rc32.ll -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=nano -verify-machineinstrs %t/rc16.ll -o /dev/null

; Overflow after incrementing an unsigned word is simply a test for zero.
; Legalization must not materialize a boolean merely to branch on it. The
; updated word belongs in r0 and the pointer update fills the load-use gap.
; CHECK-LABEL: increment:
; CHECK-NOT: sltu
; CHECK: ld r0,
; CHECK: addi r0, 1
; CHECK-NOT: sltu
; CHECK: st r0,
; CHECK: beqz
; CHECK-NOT: sltu
; CHECK: ret

; Inversion of the predicate must invert the branch as well.
; CHECK-LABEL: until_wrap:
; CHECK-NOT: sltu
; CHECK: ld r0,
; CHECK: addi r0, 1
; CHECK-NOT: sltu
; CHECK: st r0,
; CHECK: bnez
; CHECK-NOT: sltu
; CHECK: ret

; When the overflow value is stored too, its boolean value is still required.
; CHECK-LABEL: save_overflow:
; CHECK: sltu

;--- rc16.ll
declare { i16, i1 } @llvm.uadd.with.overflow.i16(i16, i16)
define void @increment(ptr %p) {
entry:
  br label %loop
loop:
  %ptr = phi ptr [ %p, %entry ], [ %next, %loop ]
  %x = load volatile i16, ptr %ptr
  %pair = call { i16, i1 } @llvm.uadd.with.overflow.i16(i16 %x, i16 1)
  %v = extractvalue { i16, i1 } %pair, 0
  %ov = extractvalue { i16, i1 } %pair, 1
  store volatile i16 %v, ptr %ptr
  %next = getelementptr i16, ptr %ptr, i16 1
  br i1 %ov, label %loop, label %exit
exit:
  ret void
}
define void @until_wrap(ptr %p) {
entry:
  br label %loop
loop:
  %ptr = phi ptr [ %p, %entry ], [ %next, %loop ]
  %x = load volatile i16, ptr %ptr
  %pair = call { i16, i1 } @llvm.uadd.with.overflow.i16(i16 %x, i16 1)
  %v = extractvalue { i16, i1 } %pair, 0
  %ov = extractvalue { i16, i1 } %pair, 1
  store volatile i16 %v, ptr %ptr
  %next = getelementptr i16, ptr %ptr, i16 1
  %continue = xor i1 %ov, true
  br i1 %continue, label %loop, label %exit
exit:
  ret void
}
define i16 @save_overflow(i16 %x, ptr %out) {
  %pair = call { i16, i1 } @llvm.uadd.with.overflow.i16(i16 %x, i16 1)
  %v = extractvalue { i16, i1 } %pair, 0
  %ov = extractvalue { i16, i1 } %pair, 1
  %b = zext i1 %ov to i16
  store volatile i16 %b, ptr %out
  ret i16 %v
}

;--- rc32.ll
declare { i32, i1 } @llvm.uadd.with.overflow.i32(i32, i32)
define void @increment(ptr %p) {
entry:
  br label %loop
loop:
  %ptr = phi ptr [ %p, %entry ], [ %next, %loop ]
  %x = load volatile i32, ptr %ptr
  %pair = call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %x, i32 1)
  %v = extractvalue { i32, i1 } %pair, 0
  %ov = extractvalue { i32, i1 } %pair, 1
  store volatile i32 %v, ptr %ptr
  %next = getelementptr i32, ptr %ptr, i32 1
  br i1 %ov, label %loop, label %exit
exit:
  ret void
}
define void @until_wrap(ptr %p) {
entry:
  br label %loop
loop:
  %ptr = phi ptr [ %p, %entry ], [ %next, %loop ]
  %x = load volatile i32, ptr %ptr
  %pair = call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %x, i32 1)
  %v = extractvalue { i32, i1 } %pair, 0
  %ov = extractvalue { i32, i1 } %pair, 1
  store volatile i32 %v, ptr %ptr
  %next = getelementptr i32, ptr %ptr, i32 1
  %continue = xor i1 %ov, true
  br i1 %continue, label %loop, label %exit
exit:
  ret void
}
define i32 @save_overflow(i32 %x, ptr %out) {
  %pair = call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %x, i32 1)
  %v = extractvalue { i32, i1 } %pair, 0
  %ov = extractvalue { i32, i1 } %pair, 1
  %b = zext i1 %ov to i32
  store volatile i32 %b, ptr %out
  ret i32 %v
}
