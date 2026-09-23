; REQUIRES: riscc-registered-target
; RUN: opt -verify-each -passes=riscc-prepare -S %s | FileCheck %s
; RUN: sed 's/p:32:32/p:16:16/' %s | opt -verify-each -passes=riscc-prepare -S - | FileCheck %s

target datalayout = "e-p:32:32-i32:32-n8:16:32"
target triple = "riscc-none-elf"

@observed = external global i32
declare i32 @pure(i32) nounwind willreturn memory(none)
declare i32 @throwing(i32) willreturn memory(none)
declare i32 @reading(i32) nounwind willreturn memory(read)
declare i32 @convergent(i32) convergent nounwind willreturn memory(none)

; Three overwritten arms need neither the initial store nor its pure call.
; The fourth arm reads a global that can alias %out: keep the original value
; visible before that load. The default arm needs the initial value too.
define void @overwrite_arms(ptr %out, i32 %which) {
; CHECK-LABEL: define void @overwrite_arms(
; CHECK: entry:
; CHECK-NEXT: switch i32 %which, label %store.needed
; CHECK: store.needed:
; CHECK-NEXT: %{{.*}} = call i32 @pure(i32 %which)
; CHECK: store i32 %{{.*}}, ptr %out
; CHECK-NEXT: br label %exit
; CHECK: store.needed{{[0-9]+}}:
; CHECK-NEXT: %{{.*}} = call i32 @pure(i32 %which)
; CHECK: store i32 %{{.*}}, ptr %out
; CHECK-NEXT: br label %read
; CHECK: read:
; CHECK-NEXT: %value = load i32, ptr @observed
entry:
  %call = call i32 @pure(i32 %which)
  %test = icmp eq i32 %call, 0
  %initial = select i1 %test, i32 7, i32 %which
  store i32 %initial, ptr %out
  switch i32 %which, label %exit [
    i32 0, label %zero
    i32 1, label %read
    i32 2, label %two
    i32 4, label %four
  ]
zero:
  br label %write
two:
  br label %write
four:
  br label %write
read:
  %value = load i32, ptr @observed
  br label %write
write:
  %result = phi i32 [0, %zero], [2, %two], [4, %four], [%value, %read]
  store i32 %result, ptr %out
  br label %exit
exit:
  ret void
}

; Only the default edge needs the value, so this does not duplicate code even
; under size optimization. A shared input is not sunk with its single-use tree.
define i32 @one_live_edge(ptr %out, i1 %which, i32 %x) optsize {
; CHECK-LABEL: define i32 @one_live_edge(
; CHECK: %shared = add i32 %x, 1
; CHECK-NEXT: br i1 %which, label %overwrite, label %store.needed
; CHECK: store.needed:
; CHECK: call i32 @pure(i32 %shared)
entry:
  %shared = add i32 %x, 1
  %call = call i32 @pure(i32 %shared)
  %test = icmp eq i32 %call, 0
  %initial = select i1 %test, i32 7, i32 %x
  store i32 %initial, ptr %out
  br i1 %which, label %overwrite, label %exit
overwrite:
  store i32 9, ptr %out
  br label %exit
exit:
  ret i32 %shared
}

; A throwing producer cannot be skipped on the overwrite path.
define void @keep_throwing(ptr %out, i1 %which, i32 %x) {
; CHECK-LABEL: define void @keep_throwing(
; CHECK: %call = call i32 @throwing
; CHECK: store i32 %initial, ptr %out
; CHECK-NEXT: br i1 %which
entry:
  %call = call i32 @throwing(i32 %x)
  %test = icmp eq i32 %call, 0
  %initial = select i1 %test, i32 7, i32 %x
  store i32 %initial, ptr %out
  br i1 %which, label %overwrite, label %exit
overwrite:
  store i32 9, ptr %out
  br label %exit
exit:
  ret void
}

; A reading call must not move past the branch and later memory accesses.
define void @keep_reading(ptr %out, i1 %which, i32 %x) {
; CHECK-LABEL: define void @keep_reading(
; CHECK: %call = call i32 @reading
; CHECK: store i32 %initial, ptr %out
; CHECK-NEXT: br i1 %which
entry:
  %call = call i32 @reading(i32 %x)
  %test = icmp eq i32 %call, 0
  %initial = select i1 %test, i32 7, i32 %x
  store i32 %initial, ptr %out
  br i1 %which, label %overwrite, label %exit
overwrite:
  store i32 9, ptr %out
  br label %exit
exit:
  ret void
}

define void @keep_convergent(ptr %out, i1 %which, i32 %x) {
; CHECK-LABEL: define void @keep_convergent(
; CHECK: %call = call i32 @convergent
; CHECK: store i32 %initial, ptr %out
; CHECK-NEXT: br i1 %which
entry:
  %call = call i32 @convergent(i32 %x) convergent
  %test = icmp eq i32 %call, 0
  %initial = select i1 %test, i32 7, i32 %x
  store i32 %initial, ptr %out
  br i1 %which, label %overwrite, label %exit
overwrite:
  store i32 9, ptr %out
  br label %exit
exit:
  ret void
}

; A narrow write does not completely replace the original word.
define void @partial_overwrite(ptr %out, i1 %which, i32 %x) {
; CHECK-LABEL: define void @partial_overwrite(
; CHECK: %call = call i32 @pure
; CHECK: store i32 %initial, ptr %out
; CHECK-NEXT: br i1 %which
entry:
  %call = call i32 @pure(i32 %x)
  %test = icmp eq i32 %call, 0
  %initial = select i1 %test, i32 7, i32 %x
  store i32 %initial, ptr %out
  br i1 %which, label %overwrite, label %exit
overwrite:
  store i8 9, ptr %out
  br label %exit
exit:
  ret void
}

; Volatile stores are observable even when another store follows.
define void @volatile_store(ptr %out, i1 %which, i32 %x) {
; CHECK-LABEL: define void @volatile_store(
; CHECK: %call = call i32 @pure
; CHECK: store volatile i32 %initial, ptr %out
; CHECK-NEXT: br i1 %which
entry:
  %call = call i32 @pure(i32 %x)
  %test = icmp eq i32 %call, 0
  %initial = select i1 %test, i32 7, i32 %x
  store volatile i32 %initial, ptr %out
  br i1 %which, label %overwrite, label %exit
overwrite:
  store i32 9, ptr %out
  br label %exit
exit:
  ret void
}
