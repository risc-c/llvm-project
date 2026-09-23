; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC32

target triple = "riscc-none-elf"

declare float @llvm.minnum.f32(float, float)
declare double @llvm.minnum.f64(double, double)
declare float @llvm.maxnum.f32(float, float)
declare double @llvm.maxnum.f64(double, double)
declare { float, float } @llvm.modf.f32(float)
declare { double, double } @llvm.modf.f64(double)

define float @minnum_f32(float %a, float %b) {
; RC16-LABEL: minnum_f32:
; RC16:       fminf
; RC32-LABEL: minnum_f32:
; RC32:       fminf
  %result = call float @llvm.minnum.f32(float %a, float %b)
  ret float %result
}

define double @minnum_f64(double %a, double %b) {
; RC16-LABEL: minnum_f64:
; RC16:       fmin
; RC32-LABEL: minnum_f64:
; RC32:       fmin
  %result = call double @llvm.minnum.f64(double %a, double %b)
  ret double %result
}

define float @maxnum_f32(float %a, float %b) {
; RC16-LABEL: maxnum_f32:
; RC16:       fmaxf
; RC32-LABEL: maxnum_f32:
; RC32:       fmaxf
  %result = call float @llvm.maxnum.f32(float %a, float %b)
  ret float %result
}

define double @maxnum_f64(double %a, double %b) {
; RC16-LABEL: maxnum_f64:
; RC16:       fmax
; RC32-LABEL: maxnum_f64:
; RC32:       fmax
  %result = call double @llvm.maxnum.f64(double %a, double %b)
  ret double %result
}

define float @modf_f32(float %value) {
; RC16-LABEL: modf_f32:
; RC16:       modff
; RC32-LABEL: modf_f32:
; RC32:       modff
  %pair = call { float, float } @llvm.modf.f32(float %value)
  %result = extractvalue { float, float } %pair, 0
  ret float %result
}

define double @modf_f64(double %value) {
; RC16-LABEL: modf_f64:
; RC16:       modf
; RC32-LABEL: modf_f64:
; RC32:       modf
  %pair = call { double, double } @llvm.modf.f64(double %value)
  %result = extractvalue { double, double } %pair, 0
  ret double %result
}
