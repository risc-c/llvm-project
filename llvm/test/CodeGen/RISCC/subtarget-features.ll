; REQUIRES: riscc-registered-target
; RUN: split-file %s %t
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %t/features.ll -o - | FileCheck %s
; RUN: llc -enable-new-pm -mtriple=riscc -mcpu=full -verify-machineinstrs %t/features.ll -filetype=obj -o %t/features.o
; RUN: not --crash llc -mtriple=riscc -mcpu=full %t/rc32.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ABI
; RUN: not --crash llc -mtriple=riscc -mcpu=full %t/nano.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ABI

; RUN: llc -mtriple=riscc -mcpu=min -verify-machineinstrs %t/stronger.ll -filetype=obj -o %t/stronger.o
; RUN: llvm-readobj -h %t/stronger.o | FileCheck %s --check-prefix=PROFILE
; RUN: not llc -mtriple=riscc -relocation-model=pic %t/features.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=PIC
; RUN: not llc -mtriple=riscc -code-model=large %t/features.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MODEL

; PROFILE: EF_RISCC_PROFILE_FULL
; PIC: RISC-C supports only static relocation
; MODEL: RISC-C supports only the small code model

; Function attributes must override the module's default CPU and features.
; CHECK-LABEL: soft_mul:
; CHECK-NOT:   mul {{r[0-7]}}
; CHECK:       __mulhi3
; CHECK:       ret s7
; CHECK-LABEL: hard_mul:
; CHECK:       mul {{r[0-7]}}
; CHECK-NEXT:  ret s7
; CHECK-LABEL: min_mul:
; CHECK-NOT:   mul {{r[0-7]}}
; CHECK:       __mulhi3
; CHECK:       ret s7
; ABI: RISC-C function target attributes cannot change the ABI

;--- features.ll
define i16 @soft_mul(i16 %a, i16 %b) #0 {
  %v = mul i16 %a, %b
  ret i16 %v
}
define i16 @hard_mul(i16 %a, i16 %b) {
  %v = mul i16 %a, %b
  ret i16 %v
}
define i16 @min_mul(i16 %a, i16 %b) #1 {
  %v = mul i16 %a, %b
  ret i16 %v
}
attributes #0 = { "target-features"="-mul" }
attributes #1 = { "target-cpu"="min" }

;--- rc32.ll
define void @f() "target-features"="+rc32" { ret void }

;--- nano.ll
define void @f() "target-cpu"="nano" { ret void }

;--- stronger.ll
define i16 @full_mul(i16 %a, i16 %b) "target-cpu"="full" {
  %v = mul i16 %a, %b
  ret i16 %v
}
