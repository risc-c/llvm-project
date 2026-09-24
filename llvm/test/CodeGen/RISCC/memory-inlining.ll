; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC32
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=NANO

target triple = "riscc-none-elf"

declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1 immarg)
declare void @llvm.memset.p0.i32(ptr, i8, i32, i1 immarg)

@constant_bytes = private unnamed_addr constant [16 x i8] [i8 18, i8 165, i8 60, i8 219, i8 129, i8 6, i8 247, i8 72, i8 53, i8 190, i8 17, i8 226, i8 124, i8 9, i8 144, i8 203], align 1

define void @copy_constant16(ptr %dst) {
; RC32-LABEL: copy_constant16:
; RC32-NOT:   ldb
; RC32-NOT:   stb
; RC32-DAG:   ldpc {{r[0-7]}}, .Ltmp{{[0-9]+}}
; RC32-DAG:   ldpc {{r[0-7]}}, .Ltmp{{[0-9]+}}
; RC32-DAG:   ldpc {{r[0-7]}}, .Ltmp{{[0-9]+}}
; RC32-DAG:   ldpc {{r[0-7]}}, .Ltmp{{[0-9]+}}
; RC32-DAG:   st {{r[0-7]}}, [r1 + 12]
; RC32-DAG:   st {{r[0-7]}}, [r1 + 8]
; RC32-DAG:   st {{r[0-7]}}, [r1 + 4]
; RC32-DAG:   st {{r[0-7]}}, [r1 + 0]
; RC32-NOT:   ldb
; RC32-NOT:   stb
; RC32:       ret s7
; RC32-DAG:   .long 3415214460
; RC32-DAG:   .long 3792813621
; RC32-DAG:   .long 1224148609
; RC32-DAG:   .long 3678184722
; RC16-LABEL: copy_constant16:
; RC16-NOT:   ldb
; RC16-NOT:   stb
; RC16-DAG:   ldi16 [[C16_1:r[0-7]]], 52112
; RC16-DAG:   st [[C16_1]], [r1 + 14]
; RC16-DAG:   ldi16 [[C16_2:r[0-7]]], 2428
; RC16-DAG:   st [[C16_2]], [r1 + 12]
; RC16-DAG:   ldi16 [[C16_3:r[0-7]]], 57873
; RC16-DAG:   st [[C16_3]], [r1 + 10]
; RC16-DAG:   ldi16 [[C16_4:r[0-7]]], 48693
; RC16-DAG:   st [[C16_4]], [r1 + 8]
; RC16-DAG:   ldi16 [[C16_5:r[0-7]]], 18679
; RC16-DAG:   st [[C16_5]], [r1 + 6]
; RC16-DAG:   ldi16 [[C16_6:r[0-7]]], 1665
; RC16-DAG:   st [[C16_6]], [r1 + 4]
; RC16-DAG:   ldi16 [[C16_7:r[0-7]]], 56124
; RC16-DAG:   st [[C16_7]], [r1 + 2]
; RC16-DAG:   ldi16 [[C16_8:r[0-7]]], 42258
; RC16-DAG:   st [[C16_8]], [r1 + 0]
; RC16-NOT:   ldb
; RC16-NOT:   stb
; RC16:       ret s7
; NANO-LABEL: copy_constant16:
; NANO-NOT:   ldb
; NANO-NOT:   stb
; NANO:       ldi16 [[CNANO:r[0-7]]], 52112
; NANO-NEXT:  st [[CNANO]], [r1 + 14]
; NANO:       ldi16 [[CNANO:r[0-7]]], 2428
; NANO-NEXT:  st [[CNANO]], [r1 + 12]
; NANO:       ldi16 [[CNANO:r[0-7]]], 57873
; NANO-NEXT:  st [[CNANO]], [r1 + 10]
; NANO:       ldi16 [[CNANO:r[0-7]]], 48693
; NANO-NEXT:  st [[CNANO]], [r1 + 8]
; NANO:       ldi16 [[CNANO:r[0-7]]], 18679
; NANO-NEXT:  st [[CNANO]], [r1 + 6]
; NANO:       ldi16 [[CNANO:r[0-7]]], 1665
; NANO-NEXT:  st [[CNANO]], [r1 + 4]
; NANO:       ldi16 [[CNANO:r[0-7]]], 56124
; NANO-NEXT:  st [[CNANO]], [r1 + 2]
; NANO:       ldi16 [[CNANO:r[0-7]]], 42258
; NANO-NEXT:  st [[CNANO]], [r1 + 0]
; NANO-NOT:   ldb
; NANO-NOT:   stb
; NANO:       jalr r0, r6
  %src = getelementptr inbounds [16 x i8], ptr @constant_bytes, i32 0, i32 0
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 1 %src, i32 16, i1 false)
  ret void
}

define void @copy64(ptr %dst, ptr %src) {
; RC32-LABEL: copy64:
; RC32:       ld {{r[0-7]}}, [r2 + 0]
; RC32:       st {{r[0-7]}}, [r1 + 0]
; RC32:       ld {{r[0-7]}}, [r2 + 60]
; RC32:       st {{r[0-7]}}, [r1 + 60]
; RC32-NOT:   call_target(memcpy)
; RC32:       ret s7
; NANO-LABEL: copy64:
; NANO:       ldi r3, 64
; NANO:       ldi16 r0, memcpy
; NANO:       jalr r6, r0
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 64, i1 false)
  ret void
}

define void @set64(ptr %dst) {
; RC32-LABEL: set64:
; RC32:       ldpc [[SET64_VALUE:r[0-7]]], .Ltmp{{[0-9]+}}
; RC32:       st [[SET64_VALUE]], [r1 + 60]
; RC32:       st [[SET64_VALUE]], [r1 + 0]
; RC32:       ret s7
; NANO-LABEL: set64:
; NANO:       ldi r3, 64
; NANO:       ldi16 r0, memset
; NANO:       jalr r6, r0
  call void @llvm.memset.p0.i32(ptr align 4 %dst, i8 90, i32 64, i1 false)
  ret void
}

define void @copy68(ptr %dst, ptr %src) {
; RC32-LABEL: copy68:
; RC32:       ldi r3, 68
; RC32:       jalr s7, r0
; RC32:       .long call_target(memcpy)
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 68, i1 false)
  ret void
}

define void @set68(ptr %dst) {
; RC32-LABEL: set68:
; RC32:       ldi r3, 68
; RC32:       jalr s7, r0
; RC32:       .long call_target(memset)
  call void @llvm.memset.p0.i32(ptr align 4 %dst, i8 90, i32 68, i1 false)
  ret void
}

define void @copy16_unaligned(ptr %dst, ptr %src) {
; RC32-LABEL: copy16_unaligned:
; RC32-NOT:   {{^[[:space:]]+ld[[:space:]]}}
; RC32-NOT:   {{^[[:space:]]+st[[:space:]]}}
; RC32:       ldb {{r[0-7]}}, [{{r[0-7]}}]
; RC32:       stb {{r[0-7]}}, [{{r[0-7]}}]
; RC32:       ret s7
  call void @llvm.memcpy.p0.p0.i32(ptr align 1 %dst, ptr align 1 %src, i32 16, i1 false)
  ret void
}

define void @copy64_size(ptr %dst, ptr %src) optsize {
; RC32-LABEL: copy64_size:
; RC32:       ldi r3, 64
; RC32:       jalr s7, r0
; RC32:       .long call_target(memcpy)
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 64, i1 false)
  ret void
}

define void @set64_size(ptr %dst) optsize {
; RC32-LABEL: set64_size:
; RC32:       ldi r3, 64
; RC32:       jalr s7, r0
; RC32:       .long call_target(memset)
  call void @llvm.memset.p0.i32(ptr align 4 %dst, i8 90, i32 64, i1 false)
  ret void
}

define void @copy64_minsize(ptr %dst, ptr %src) minsize {
; RC32-LABEL: copy64_minsize:
; RC32:       ldi r3, 64
; RC32:       jalr s7, r0
; RC32:       .long call_target(memcpy)
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 64, i1 false)
  ret void
}

define void @set64_minsize(ptr %dst) minsize {
; RC32-LABEL: set64_minsize:
; RC32:       ldi r3, 64
; RC32:       jalr s7, r0
; RC32:       .long call_target(memset)
  call void @llvm.memset.p0.i32(ptr align 4 %dst, i8 90, i32 64, i1 false)
  ret void
}

define void @copy18_aligned(ptr %dst, ptr %src) {
; RC16-LABEL: copy18_aligned:
; RC16-DAG:   ld r{{[0-7]}}, [r2 + 16]
; RC16-DAG:   st r{{[0-7]}}, [r1 + 16]
; RC16-DAG:   ld r{{[0-7]}}, [r2 + 0]
; RC16-DAG:   st r{{[0-7]}}, [r1 + 0]
; RC16:       ret s7
; NANO-LABEL: copy18_aligned:
; NANO:       ldi r3, 18
; NANO:       ldi16 r0, memcpy
; NANO:       jalr r6, r0
  call void @llvm.memcpy.p0.p0.i32(ptr align 2 %dst, ptr align 2 %src, i32 18, i1 false)
  ret void
}

define void @set18_aligned(ptr %dst) {
; RC16-LABEL: set18_aligned:
; RC16:       ldi16 r{{[0-7]}}, 23130
; RC16:       st r{{[0-7]}}, [r1 + 16]
; RC16:       st r{{[0-7]}}, [r1 + 0]
; RC16:       ret s7
; NANO-LABEL: set18_aligned:
; NANO:       ldi r3, 18
; NANO:       ldi16 r0, memset
; NANO:       jalr r6, r0
  call void @llvm.memset.p0.i32(ptr align 2 %dst, i8 90, i32 18, i1 false)
  ret void
}

define void @copy32_aligned(ptr %dst, ptr %src) {
; RC16-LABEL: copy32_aligned:
; RC16-DAG:   ld r{{[0-7]}}, [r2 + 30]
; RC16-DAG:   st r{{[0-7]}}, [r1 + 30]
; RC16-DAG:   ld r{{[0-7]}}, [r2 + 0]
; RC16-DAG:   st r{{[0-7]}}, [r1 + 0]
; RC16:       ret s7
  call void @llvm.memcpy.p0.p0.i32(ptr align 2 %dst, ptr align 2 %src, i32 32, i1 false)
  ret void
}

define void @set32_aligned(ptr %dst) {
; RC16-LABEL: set32_aligned:
; RC16:       ldi16 r{{[0-7]}}, 23130
; RC16:       st r{{[0-7]}}, [r1 + 30]
; RC16:       st r{{[0-7]}}, [r1 + 0]
; RC16:       ret s7
  call void @llvm.memset.p0.i32(ptr align 2 %dst, i8 90, i32 32, i1 false)
  ret void
}

define void @copy34_aligned(ptr %dst, ptr %src) {
; RC16-LABEL: copy34_aligned:
; RC16:       ldi r3, 34
; RC16:       jall s7, memcpy
  call void @llvm.memcpy.p0.p0.i32(ptr align 2 %dst, ptr align 2 %src, i32 34, i1 false)
  ret void
}

define void @set34_aligned(ptr %dst) {
; RC16-LABEL: set34_aligned:
; RC16:       ldi r3, 34
; RC16:       jall s7, memset
  call void @llvm.memset.p0.i32(ptr align 2 %dst, i8 90, i32 34, i1 false)
  ret void
}
