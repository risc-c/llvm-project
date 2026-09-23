; REQUIRES: riscc-registered-target
; RUN: opt -verify-each -passes=riscc-prepare -S %s | FileCheck %s --check-prefixes=COMMON,RC16
; RUN: sed 's/p:16:16/p:32:32/' %s | opt -verify-each -passes=riscc-prepare -S - | FileCheck %s --check-prefixes=COMMON,RC32

target datalayout = "e-m:e-p:16:16-i8:8-i16:16-i32:16-i64:16-n8:16-S16"
target triple = "riscc-none-elf"

; A loop-carried GEP is computed in the preheader and reused by the next
; iteration.  The original inbounds proofs are deliberately weakened.
define i16 @reuse_gep(ptr %base, i16 %limit) {
entry:
; COMMON-LABEL: define i16 @reuse_gep
; COMMON:       %[[INIT:[^ ]+]] = getelementptr i16, ptr %base, i16 0
; COMMON:       %address.carried = phi ptr [ %[[INIT]], %entry ], [ %backaddr, %latch ]
; COMMON-NOT:   getelementptr inbounds i16, ptr %base
; COMMON:       %value = load volatile i16, ptr %address.carried
; COMMON:       %backaddr = getelementptr i16, ptr %base, i16 %next
; COMMON-NEXT:  store volatile i16 %value, ptr %backaddr
  br label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ %next, %latch ]
  %addr = getelementptr inbounds i16, ptr %base, i16 %i
  %value = load volatile i16, ptr %addr
  br label %latch

latch:
  %next = add i16 %i, 1
  %backaddr = getelementptr inbounds i16, ptr %base, i16 %next
  store volatile i16 %value, ptr %backaddr
  %again = icmp ult i16 %next, %limit
  br i1 %again, label %loop, label %exit

exit:
  ret i16 0
}

; A different base prevents carrying the backedge address.
define i16 @different_base(ptr %base, ptr %other, i16 %limit) {
entry:
; COMMON-LABEL: define i16 @different_base
; COMMON:       %addr = getelementptr inbounds i16, ptr %base, i16 %i
; COMMON:       %backaddr = getelementptr inbounds i16, ptr %other, i16 %next
  br label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ %next, %latch ]
  %addr = getelementptr inbounds i16, ptr %base, i16 %i
  %value = load i16, ptr %addr
  br label %latch

latch:
  %next = add i16 %i, 1
  %backaddr = getelementptr inbounds i16, ptr %other, i16 %next
  store i16 %value, ptr %backaddr
  %again = icmp ult i16 %next, %limit
  br i1 %again, label %loop, label %exit

exit:
  ret i16 0
}

; A different source element type prevents carrying the address.
define i16 @different_stride(ptr %base, i16 %limit) {
entry:
; COMMON-LABEL: define i16 @different_stride
; COMMON:       %addr = getelementptr inbounds i16, ptr %base, i16 %i
; COMMON:       %backaddr = getelementptr inbounds i32, ptr %base, i16 %next
  br label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ %next, %latch ]
  %addr = getelementptr inbounds i16, ptr %base, i16 %i
  %value = load i16, ptr %addr
  br label %latch

latch:
  %next = add i16 %i, 1
  %backaddr = getelementptr inbounds i32, ptr %base, i16 %next
  store i32 0, ptr %backaddr
  %again = icmp ult i16 %next, %limit
  br i1 %again, label %loop, label %exit

exit:
  ret i16 0
}

; A matching backedge GEP in a side block does not dominate the latch.
define i16 @nondominating_backgep(ptr %base, i1 %condition) {
entry:
; COMMON-LABEL: define i16 @nondominating_backgep
; COMMON:       %addr = getelementptr inbounds i16, ptr %base, i16 %i
; COMMON:       %backaddr = getelementptr inbounds i16, ptr %base, i16 %next
  %next = add i16 0, 1
  br label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ %next, %latch ]
  %addr = getelementptr inbounds i16, ptr %base, i16 %i
  br i1 %condition, label %side, label %latch

side:
  %backaddr = getelementptr inbounds i16, ptr %base, i16 %next
  store i16 0, ptr %backaddr
  br label %latch

latch:
  br i1 %condition, label %loop, label %exit

exit:
  ret i16 0
}

; A constant backedge index must be ignored without attempting a cast.
define i16 @constant_backedge(ptr %base, i1 %again) {
entry:
; COMMON-LABEL: define i16 @constant_backedge
; COMMON:       %addr = getelementptr inbounds i16, ptr %base, i16 %i
  br label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ 0, %latch ]
  %addr = getelementptr inbounds i16, ptr %base, i16 %i
  %value = load i16, ptr %addr
  br label %latch

latch:
  br i1 %again, label %loop, label %exit

exit:
  ret i16 %value
}

; Equal constants from separate return edges share one native-width return
; block.  The RC16 run also proves that an i32 return is left alone here.
define i16 @same_constant(i1 %condition) {
entry:
; COMMON-LABEL: define i16 @same_constant
; COMMON:       left:
; COMMON:       br label %return.constant
; COMMON:       right:
; COMMON:       br label %return.constant
; COMMON:       return.constant:
; COMMON-NEXT:  ret i16 7
  br i1 %condition, label %left, label %right

left:
  br label %ret

right:
  br label %ret

ret:
  %value = phi i16 [ 7, %left ], [ 7, %right ]
  ret i16 %value
}

; A nonconstant return edge remains on the shared return block.
define i16 @mixed_constant(i1 %condition, i16 %dynamic) {
; COMMON-LABEL: define i16 @mixed_constant
; COMMON:       left:
; COMMON:       br label %return.constant
; COMMON:       right:
; COMMON:       br label %ret
; COMMON:       ret:
; COMMON-NEXT:  ret i16 %dynamic
; COMMON:       return.constant:
; COMMON-NEXT:  ret i16 7
  br i1 %condition, label %left, label %right

left:
  br label %ret

right:
  br label %ret

ret:
  %value = phi i16 [ 7, %left ], [ %dynamic, %right ]
  ret i16 %value
}

define i32 @same_constant32(i1 %condition) {
entry:
; RC16-LABEL: define i32 @same_constant32
; RC16:       %value = phi i32 [ 7, %left ], [ 7, %right ]
; RC32-LABEL: define i32 @same_constant32
; RC32:       left:
; RC32:       br label %return.constant
; RC32:       right:
; RC32:       br label %return.constant
; RC32:       return.constant:
; RC32-NEXT:  ret i32 7
  br i1 %condition, label %left, label %right

left:
  br label %ret

right:
  br label %ret

ret:
  %value = phi i32 [ 7, %left ], [ 7, %right ]
  ret i32 %value
}

; Two CFG edges from one predecessor are not eligible for splitting.
define i16 @duplicate_edges(i1 %condition) {
entry:
; COMMON-LABEL: define i16 @duplicate_edges
; COMMON:       br i1 %condition, label %ret, label %ret
; COMMON:       %value = phi i16 [ 7, %entry ], [ 7, %entry ]
  br i1 %condition, label %ret, label %ret

ret:
  %value = phi i16 [ 7, %entry ], [ 7, %entry ]
  ret i16 %value
}

; A block address makes the return block externally addressable.
@address_taken_ptr = constant ptr blockaddress(@address_taken, %ret)

define i16 @address_taken(i1 %condition) {
entry:
; COMMON-LABEL: define i16 @address_taken
; COMMON:       %value = phi i16 [ 7, %entry ], [ 9, %other ]
  br i1 %condition, label %ret, label %other

other:
  br label %ret

ret:
  %value = phi i16 [ 7, %entry ], [ 9, %other ]
  ret i16 %value
}
