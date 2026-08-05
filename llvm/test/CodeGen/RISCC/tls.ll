; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full < %s | FileCheck %s

target datalayout = "e-m:e-P1-p:16:16-p1:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:8:16-n8:16-S16"
target triple = "riscc-none-elf"

@tls_initialized = thread_local global i16 4660, align 2
@tls_zeroed = thread_local global i16 0, align 2

define i16 @read_tls() {
; CHECK-LABEL: read_tls:
; CHECK:       mfs{{.*}}s2
; CHECK:       li{{.*}}tpoff(tls_zeroed)
; CHECK-NEXT:  ldx
; CHECK:       li{{.*}}tpoff(tls_initialized)
; CHECK-NEXT:  ldx
; CHECK-NEXT:  add
  %a = load i16, ptr @tls_initialized, align 2
  %b = load i16, ptr @tls_zeroed, align 2
  %sum = add i16 %a, %b
  ret i16 %sum
}
