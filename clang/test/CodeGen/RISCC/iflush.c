// REQUIRES: riscc-registered-target
// RUN: %clang_cc1 -triple riscc -target-cpu sys -target-feature +iflush -mrelocation-model static -emit-obj %s -o %t.sys.o
// RUN: %clang_cc1 -triple riscc -target-cpu full -target-feature +iflush -mrelocation-model static -emit-obj %s -o %t.full.o
// RUN: %clang_cc1 -triple riscc -target-cpu sys -target-feature +rc32 -target-feature +iflush -mrelocation-model static -emit-obj %s -o %t.rc32-sys.o
// RUN: %clang_cc1 -triple riscc -target-cpu full -target-feature +rc32 -target-feature +iflush -mrelocation-model static -emit-obj %s -o %t.rc32-full.o
// RUN: not %clang_cc1 -triple riscc -target-cpu nano -target-feature +sys -target-feature +iflush -mrelocation-model static -emit-obj %s -o %t.nano.o 2>&1 | FileCheck %s

void flush(void) {
  __asm__ volatile ("iflush" ::: "memory");
}

// CHECK: error: invalid operand for RISC-C instruction
