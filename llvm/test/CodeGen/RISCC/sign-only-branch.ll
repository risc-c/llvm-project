; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC16

define void @negative_half(i32 %x, ptr %out) {
; CHECK-LABEL: negative_half:
; CHECK: slli r0, r1, 8
; CHECK-NEXT: slli r0, r0, 8
; CHECK-NOT: srai
; CHECK: b{{lt|ge}}z
; CHECK-NOT: srai
; CHECK: .Lfunc_end
  %n = trunc i32 %x to i16
  %negative = icmp slt i16 %n, 0
  br i1 %negative, label %yes, label %done
yes:
  store volatile i32 %x, ptr %out
  br label %done
done:
  ret void
}

define void @nonnegative_half(i32 %x, ptr %out) {
; CHECK-LABEL: nonnegative_half:
; CHECK: slli r0, r1, 8
; CHECK-NEXT: slli r0, r0, 8
; CHECK-NOT: srai
; CHECK: b{{lt|ge}}z
; CHECK-NOT: srai
; CHECK: .Lfunc_end
  %n = trunc i32 %x to i16
  %nonnegative = icmp sge i16 %n, 0
  br i1 %nonnegative, label %yes, label %done
yes:
  store volatile i32 %x, ptr %out
  br label %done
done:
  ret void
}

; An extension also consumed as a number must remain valid on both paths.
define i32 @shared_extension(i32 %x, ptr %out) {
; CHECK-LABEL: shared_extension:
; CHECK: srai
; CHECK: srai
; CHECK: ret
  %n = trunc i32 %x to i16
  %s = sext i16 %n to i32
  %negative = icmp slt i32 %s, 0
  br i1 %negative, label %yes, label %done
yes:
  store volatile i32 %s, ptr %out
  br label %done
done:
  ret i32 %s
}

define void @negative_byte(i16 %x, ptr %out) {
; RC16-LABEL: negative_byte:
; RC16: slli r0, r1, 8
; RC16-NOT: srai
; RC16: b{{lt|ge}}z
; RC16-NOT: srai
; RC16: .Lfunc_end
  %n = trunc i16 %x to i8
  %negative = icmp slt i8 %n, 0
  br i1 %negative, label %yes, label %done
yes:
  store volatile i16 %x, ptr %out
  br label %done
done:
  ret void
}
