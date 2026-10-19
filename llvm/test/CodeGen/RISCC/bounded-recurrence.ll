; REQUIRES: riscc-registered-target
; RUN: opt -verify-each -passes=riscc-prepare -S %s | FileCheck %s --check-prefixes=IR,RC32
; RUN: sed 's/p:32:32/p:16:16/' %s | opt -verify-each -passes=riscc-prepare -S - | FileCheck %s --check-prefixes=IR,RC16
; RUN: llc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mcpu=full -verify-machineinstrs %s -o /dev/null

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"
target triple = "riscc-none-elf"

; Right shifts and XOR with a narrow constant cannot set upper bits. Extend
; the incoming value once; do not normalize every trip around the loop.
define i8 @byte_shift(i8 %start, i32 %count, i1 %choose) {
; IR-LABEL: define i8 @byte_shift
; IR:       zext i8 %start to i{{16|32}}
; IR:       %value.wide = phi i{{16|32}}
; IR:       %shift.wide = lshr i{{16|32}} %value.wide, 1
; IR:       %xor.wide = xor i{{16|32}} %shift.wide, 156
; IR:       %next.wide = select i1 %choose, i{{16|32}} %xor.wide, i{{16|32}} %shift.wide
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %shift = lshr i8 %value, 1
  %xor = xor i8 %shift, 156
  %next = select i1 %choose, i8 %xor, i8 %shift
  %inc = add i32 %i, 1
  %done = icmp eq i32 %inc, %count
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; A low-byte observation inside the loop does not require a full halfword
; normalization. The comparison remains narrow and observes exactly that byte.
define i16 @half_shift(i16 %start, i32 %count, ptr %out) {
; IR-LABEL: define i16 @half_shift
; RC32:     zext i16 %start to i32
; RC32:     %value.wide = phi i32
; RC32:     %shift.wide = lshr i32 %value.wide, 1
; RC32:     %next.wide = xor i32 %shift.wide, 46080
; RC32:     trunc i32 %next.wide to i16
; RC16:     %value = phi i16
; RC16:     %shift = lshr i16 %value, 1
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %value = phi i16 [ %start, %entry ], [ %next, %loop ]
  %shift = lshr i16 %value, 1
  %next = xor i16 %shift, 46080
  %byte = trunc i16 %next to i8
  store volatile i8 %byte, ptr %out
  %inc = add i32 %i, 1
  %done = icmp eq i32 %inc, %count
  br i1 %done, label %exit, label %loop
exit:
  ret i16 %next
}

; Wrapping arithmetic followed by a right shift is not zero-preserving.
define i8 @wrapping_shift(i8 %start, i1 %done) {
; IR-LABEL: define i8 @wrapping_shift
; IR:       %value = phi i8
; IR:       %sum = add i8 %value, 10
; IR:       %next = lshr i8 %sum, 1
entry:
  br label %loop
loop:
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %sum = add i8 %value, 10
  %next = lshr i8 %sum, 1
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; No nuw flags are needed when known operand ranges prove no overflow.
define i8 @bounded_add(i8 %start, i1 %done) {
; IR-LABEL: define i8 @bounded_add
; IR:       %value.wide = phi i{{16|32}}
; IR:       %low.wide = and i{{16|32}} %value.wide, 63
; IR:       %sum.wide = add i{{16|32}} %low.wide, 17
; IR:       %next.wide = lshr i{{16|32}} %sum.wide, 1
entry:
  br label %loop
loop:
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %low = and i8 %value, 63
  %sum = add i8 %low, 17
  %next = lshr i8 %sum, 1
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

define i8 @bounded_sub(i8 %start, i1 %done) {
; IR-LABEL: define i8 @bounded_sub
; IR:       %value.wide = phi i{{16|32}}
; IR:       %high.wide = or i{{16|32}} %value.wide, 128
; IR:       %diff.wide = sub i{{16|32}} %high.wide, 17
; IR:       %next.wide = lshr i{{16|32}} %diff.wide, 1
entry:
  br label %loop
loop:
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %high = or i8 %value, 128
  %diff = sub i8 %high, 17
  %next = lshr i8 %diff, 1
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

define i8 @bounded_mul(i8 %start, i1 %done) {
; IR-LABEL: define i8 @bounded_mul
; IR:       %value.wide = phi i{{16|32}}
; IR:       %low.wide = and i{{16|32}} %value.wide, 15
; IR:       %product.wide = mul i{{16|32}} %low.wide, 17
; IR:       %next.wide = lshr i{{16|32}} %product.wide, 1
entry:
  br label %loop
loop:
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %low = and i8 %value, 15
  %product = mul i8 %low, 17
  %next = lshr i8 %product, 1
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; A bounded variable count is legal when zero-preserving inputs and the
; maximum count prove that no source bit can shift out of the narrow width.
define i8 @bounded_variable_shift(i8 %start, i8 %amount, i1 %done) {
; IR-LABEL: define i8 @bounded_variable_shift
; IR:       %value.wide = phi i{{16|32}}
; IR:       %low.wide = and i{{16|32}} %value.wide, 15
; IR:       %count.wide = and i{{16|32}} {{.*}}, 3
; IR:       %shift.wide = shl i{{16|32}} %low.wide, %count.wide
; IR:       %next.wide = lshr i{{16|32}} %shift.wide, 1
entry:
  br label %loop
loop:
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %low = and i8 %value, 15
  %count = and i8 %amount, 3
  %shift = shl i8 %low, %count
  %next = lshr i8 %shift, 1
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; A nearby bound which permits overflow must not widen, including when
; overflow would subsequently be shifted back into the observable low bits.
define i8 @overflowing_variable_shift(i8 %start, i8 %amount, i1 %done) {
; IR-LABEL: define i8 @overflowing_variable_shift
; IR:       %value = phi i8
; IR:       %low = and i8 %value, 63
; IR:       %count = and i8 %amount, 3
; IR:       %shift = shl i8 %low, %count
; IR:       %next = lshr i8 %shift, 1
entry:
  br label %loop
loop:
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %low = and i8 %value, 63
  %count = and i8 %amount, 3
  %shift = shl i8 %low, %count
  %next = lshr i8 %shift, 1
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}
