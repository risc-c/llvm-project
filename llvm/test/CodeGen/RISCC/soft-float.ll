; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -verify-machineinstrs < %s | FileCheck %s

target triple = "riscc-none-elf"

define float @arithmetic_f32(float %a, float %b) {
; CHECK-LABEL: arithmetic_f32:
; CHECK: __addsf3
; CHECK: __subsf3
; CHECK: __mulsf3
; CHECK: __divsf3
  %add = fadd float %a, %b
  %sub = fsub float %add, %b
  %mul = fmul float %sub, %a
  %div = fdiv float %mul, %b
  ret float %div
}

define double @arithmetic_f64(double %a, double %b) {
; CHECK-LABEL: arithmetic_f64:
; CHECK: __adddf3
; CHECK: __subdf3
; CHECK: __muldf3
; CHECK: __divdf3
  %add = fadd double %a, %b
  %sub = fsub double %add, %b
  %mul = fmul double %sub, %a
  %div = fdiv double %mul, %b
  ret double %div
}

define double @extend(float %value) {
; CHECK-LABEL: extend:
; CHECK: __extendsfdf2
  %result = fpext float %value to double
  ret double %result
}

define float @double_to_float(double %value) {
; CHECK-LABEL: double_to_float:
; CHECK: __truncdfsf2
  %result = fptrunc double %value to float
  ret float %result
}

define i32 @float_to_i32(float %value) {
; CHECK-LABEL: float_to_i32:
; CHECK: __fixsfsi
  %result = fptosi float %value to i32
  ret i32 %result
}

define i64 @float_to_i64(float %value) {
; CHECK-LABEL: float_to_i64:
; CHECK: __fixsfdi
  %result = fptosi float %value to i64
  ret i64 %result
}

define i32 @double_to_i32(double %value) {
; CHECK-LABEL: double_to_i32:
; CHECK: __fixdfsi
  %result = fptosi double %value to i32
  ret i32 %result
}

define i64 @double_to_i64(double %value) {
; CHECK-LABEL: double_to_i64:
; CHECK: __fixdfdi
  %result = fptosi double %value to i64
  ret i64 %result
}

define i32 @float_to_u32(float %value) {
; CHECK-LABEL: float_to_u32:
; CHECK: __fixunssfsi
  %result = fptoui float %value to i32
  ret i32 %result
}

define i64 @float_to_u64(float %value) {
; CHECK-LABEL: float_to_u64:
; CHECK: __fixunssfdi
  %result = fptoui float %value to i64
  ret i64 %result
}

define i32 @double_to_u32(double %value) {
; CHECK-LABEL: double_to_u32:
; CHECK: __fixunsdfsi
  %result = fptoui double %value to i32
  ret i32 %result
}

define i64 @double_to_u64(double %value) {
; CHECK-LABEL: double_to_u64:
; CHECK: __fixunsdfdi
  %result = fptoui double %value to i64
  ret i64 %result
}

define float @i32_to_float(i32 %value) {
; CHECK-LABEL: i32_to_float:
; CHECK: __floatsisf
  %result = sitofp i32 %value to float
  ret float %result
}

define double @i32_to_double(i32 %value) {
; CHECK-LABEL: i32_to_double:
; CHECK: __floatsidf
  %result = sitofp i32 %value to double
  ret double %result
}

define float @i64_to_float(i64 %value) {
; CHECK-LABEL: i64_to_float:
; CHECK: __floatdisf
  %result = sitofp i64 %value to float
  ret float %result
}

define double @i64_to_double(i64 %value) {
; CHECK-LABEL: i64_to_double:
; CHECK: __floatdidf
  %result = sitofp i64 %value to double
  ret double %result
}

define float @u32_to_float(i32 %value) {
; CHECK-LABEL: u32_to_float:
; CHECK: __floatunsisf
  %result = uitofp i32 %value to float
  ret float %result
}

define double @u32_to_double(i32 %value) {
; CHECK-LABEL: u32_to_double:
; CHECK: __floatunsidf
  %result = uitofp i32 %value to double
  ret double %result
}

define float @u64_to_float(i64 %value) {
; CHECK-LABEL: u64_to_float:
; CHECK: __floatundisf
  %result = uitofp i64 %value to float
  ret float %result
}

define double @u64_to_double(i64 %value) {
; CHECK-LABEL: u64_to_double:
; CHECK: __floatundidf
  %result = uitofp i64 %value to double
  ret double %result
}

define i16 @compare_olt_f32(float %a, float %b) {
; CHECK-LABEL: compare_olt_f32:
; CHECK: __ltsf2
  %result = fcmp olt float %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_oge_f32(float %a, float %b) {
; CHECK-LABEL: compare_oge_f32:
; CHECK: __gesf2
  %result = fcmp oge float %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_ole_f32(float %a, float %b) {
; CHECK-LABEL: compare_ole_f32:
; CHECK: __lesf2
  %result = fcmp ole float %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_ogt_f32(float %a, float %b) {
; CHECK-LABEL: compare_ogt_f32:
; CHECK: __gtsf2
  %result = fcmp ogt float %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_oeq_f32(float %a, float %b) {
; CHECK-LABEL: compare_oeq_f32:
; CHECK: __eqsf2
  %result = fcmp oeq float %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_une_f32(float %a, float %b) {
; CHECK-LABEL: compare_une_f32:
; CHECK: __nesf2
  %result = fcmp une float %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_uno_f32(float %a, float %b) {
; CHECK-LABEL: compare_uno_f32:
; CHECK: __unordsf2
  %result = fcmp uno float %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_olt_f64(double %a, double %b) {
; CHECK-LABEL: compare_olt_f64:
; CHECK: __ltdf2
  %result = fcmp olt double %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_oge_f64(double %a, double %b) {
; CHECK-LABEL: compare_oge_f64:
; CHECK: __gedf2
  %result = fcmp oge double %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_ole_f64(double %a, double %b) {
; CHECK-LABEL: compare_ole_f64:
; CHECK: __ledf2
  %result = fcmp ole double %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_ogt_f64(double %a, double %b) {
; CHECK-LABEL: compare_ogt_f64:
; CHECK: __gtdf2
  %result = fcmp ogt double %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_oeq_f64(double %a, double %b) {
; CHECK-LABEL: compare_oeq_f64:
; CHECK: __eqdf2
  %result = fcmp oeq double %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_une_f64(double %a, double %b) {
; CHECK-LABEL: compare_une_f64:
; CHECK: __nedf2
  %result = fcmp une double %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

define i16 @compare_uno_f64(double %a, double %b) {
; CHECK-LABEL: compare_uno_f64:
; CHECK: __unorddf2
  %result = fcmp uno double %a, %b
  %extended = zext i1 %result to i16
  ret i16 %extended
}

declare float @llvm.sqrt.f32(float)
declare float @llvm.ceil.f32(float)
declare float @llvm.trunc.f32(float)
declare float @llvm.round.f32(float)
declare float @llvm.floor.f32(float)
declare float @llvm.copysign.f32(float, float)
declare double @llvm.sqrt.f64(double)
declare double @llvm.ceil.f64(double)
declare double @llvm.trunc.f64(double)
declare double @llvm.round.f64(double)
declare double @llvm.floor.f64(double)
declare double @llvm.copysign.f64(double, double)

define float @remainder_f32(float %a, float %b) {
; CHECK-LABEL: remainder_f32:
; CHECK: fmodf
  %result = frem float %a, %b
  ret float %result
}

define float @sqrt_f32(float %value) {
; CHECK-LABEL: sqrt_f32:
; CHECK: sqrtf
  %result = call float @llvm.sqrt.f32(float %value)
  ret float %result
}

define float @ceil_f32(float %value) {
; CHECK-LABEL: ceil_f32:
; CHECK: ceilf
  %result = call float @llvm.ceil.f32(float %value)
  ret float %result
}

define float @trunc_f32(float %value) {
; CHECK-LABEL: trunc_f32:
; CHECK: truncf
  %result = call float @llvm.trunc.f32(float %value)
  ret float %result
}

define float @round_f32(float %value) {
; CHECK-LABEL: round_f32:
; CHECK: roundf
  %result = call float @llvm.round.f32(float %value)
  ret float %result
}

define float @floor_f32(float %value) {
; CHECK-LABEL: floor_f32:
; CHECK: floorf
  %result = call float @llvm.floor.f32(float %value)
  ret float %result
}

define float @copysign_f32(float %value, float %sign) {
; CHECK-LABEL: copysign_f32:
; CHECK-NOT: call16 copysignf
; CHECK: ret
  %result = call float @llvm.copysign.f32(float %value, float %sign)
  ret float %result
}

define double @remainder_f64(double %a, double %b) {
; CHECK-LABEL: remainder_f64:
; CHECK: fmod
  %result = frem double %a, %b
  ret double %result
}

define double @sqrt_f64(double %value) {
; CHECK-LABEL: sqrt_f64:
; CHECK: sqrt
  %result = call double @llvm.sqrt.f64(double %value)
  ret double %result
}

define double @ceil_f64(double %value) {
; CHECK-LABEL: ceil_f64:
; CHECK: ceil
  %result = call double @llvm.ceil.f64(double %value)
  ret double %result
}

define double @trunc_f64(double %value) {
; CHECK-LABEL: trunc_f64:
; CHECK: trunc
  %result = call double @llvm.trunc.f64(double %value)
  ret double %result
}

define double @round_f64(double %value) {
; CHECK-LABEL: round_f64:
; CHECK: round
  %result = call double @llvm.round.f64(double %value)
  ret double %result
}

define double @floor_f64(double %value) {
; CHECK-LABEL: floor_f64:
; CHECK: floor
  %result = call double @llvm.floor.f64(double %value)
  ret double %result
}

define double @copysign_f64(double %value, double %sign) {
; CHECK-LABEL: copysign_f64:
; CHECK-NOT: call16 copysign
; CHECK: ret
  %result = call double @llvm.copysign.f64(double %value, double %sign)
  ret double %result
}
