; REQUIRES: riscc-registered-target
; RUN: opt -verify-each -passes=riscc-prepare -S %s | FileCheck %s
; RUN: sed 's/p:32:32/p:16:16/' %s | opt -verify-each -passes=riscc-prepare -S - | FileCheck %s

target datalayout = "e-p:32:32-i32:32-n8:16:32"
target triple = "riscc-none-elf"

declare i1 @consume(i8)
declare void @finish(ptr)

; One pointer survives the loop; the base is recovered only on exit.
define void @recover_base(ptr %base) {
; CHECK-LABEL: define void @recover_base(
; CHECK: %adjusted = getelementptr i8, ptr %base, i32 3
; CHECK: exit:
; CHECK-NEXT: %address.recovered = getelementptr i8, ptr %adjusted, i{{16|32}} -3
; CHECK-NEXT: call void @finish(ptr %address.recovered)
entry:
  %adjusted = getelementptr inbounds i8, ptr %base, i32 3
  br label %loop
loop:
  %byte = load i8, ptr %adjusted
  %again = call i1 @consume(i8 %byte)
  br i1 %again, label %loop, label %exit
exit:
  call void @finish(ptr %base)
  ret void
}

; Both forms are used inside the loop; recovering the base would not shorten
; its lifetime across the call.
define void @keep_live_base(ptr %base) {
; CHECK-LABEL: define void @keep_live_base(
; CHECK: %adjusted = getelementptr inbounds i8, ptr %base, i32 3
; CHECK-NOT: address.recovered
; CHECK: call void @finish(ptr %base)
entry:
  %adjusted = getelementptr inbounds i8, ptr %base, i32 3
  br label %loop
loop:
  %byte = load i8, ptr %adjusted
  %again = call i1 @consume(i8 %byte)
  call void @finish(ptr %base)
  br i1 %again, label %loop, label %exit
exit:
  call void @finish(ptr %base)
  ret void
}

; A variable displacement requires keeping another register live.
define void @keep_variable_offset(ptr %base, i32 %offset) {
; CHECK-LABEL: define void @keep_variable_offset(
; CHECK-NOT: address.recovered
; CHECK: call void @finish(ptr %base)
entry:
  %adjusted = getelementptr i8, ptr %base, i32 %offset
  br label %loop
loop:
  %byte = load i8, ptr %adjusted
  %again = call i1 @consume(i8 %byte)
  br i1 %again, label %loop, label %exit
exit:
  call void @finish(ptr %base)
  ret void
}

; Do not add exit arithmetic to a loop without calls.
define void @keep_call_free(ptr %base) {
; CHECK-LABEL: define void @keep_call_free(
; CHECK-NOT: address.recovered
; CHECK: call void @finish(ptr %base)
entry:
  %adjusted = getelementptr i8, ptr %base, i32 3
  br label %loop
loop:
  %byte = load volatile i8, ptr %adjusted
  %again = icmp ne i8 %byte, 0
  br i1 %again, label %loop, label %exit
exit:
  call void @finish(ptr %base)
  ret void
}
