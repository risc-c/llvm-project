// RUN: %clang_cc1 -triple riscc-none-elf -emit-llvm -o - %s | FileCheck %s

unsigned copy_register(unsigned value) {
  unsigned result;
  __asm__("mov %0, %1" : "=r"(result) : "r"(value));
  return result;
}

// CHECK-LABEL: define{{.*}} i16 @copy_register
// CHECK: call{{.*}} i16 asm "mov $0, $1", "=r,r"(i16
