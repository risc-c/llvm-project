; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -filetype=obj %s -o %t
; RUN: llvm-readobj -r %t | FileCheck %s --check-prefix=RELOC

target datalayout = "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-n8:16:32-S32"
target triple = "riscc-none-elf"

@tls_initialized = thread_local global i32 305419896, align 4
@tls_zeroed = thread_local global i32 0, align 4

define i32 @read_tls() {
; CHECK-DAG:   .long __riscc_current_context
; CHECK-DAG:   .long tpoff(tls_initialized)
; CHECK-DAG:   .long tpoff(tls_zeroed)
; CHECK-LABEL: read_tls:
; CHECK:       ldpc [[BASE:r[0-7]]],
; CHECK-NEXT:  ld [[BASE]], {{\[}}[[BASE]] + 0]
; CHECK-NOT:   mfs {{.*}}s2
; CHECK:       ldpc
; CHECK:       ldpc
; CHECK:       add
  %a = load i32, ptr @tls_initialized, align 4
  %b = load i32, ptr @tls_zeroed, align 4
  %sum = add i32 %a, %b
  ret i32 %sum
}

; RELOC-DAG: R_RISCC_ABS32 __riscc_current_context
; RELOC-DAG: R_RISCC_TPOFF32 tls_initialized
; RELOC-DAG: R_RISCC_TPOFF32 tls_zeroed
