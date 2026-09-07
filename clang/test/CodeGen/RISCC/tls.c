// RUN: %clang_cc1 -mrelocation-model static -triple riscc-none-elf -target-cpu full -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s

// CHECK: @tls_initialized = dso_local thread_local global i16 4660, align 2
// CHECK: @tls_zeroed = dso_local thread_local global i16 0, align 2
__thread unsigned short tls_initialized = 0x1234;
_Thread_local unsigned short tls_zeroed;

// CHECK-LABEL: define{{.*}} i16 @read_tls()
// CHECK: @llvm.threadlocal.address.p0(ptr align 2 @tls_initialized)
// CHECK: load i16
// CHECK: @llvm.threadlocal.address.p0(ptr align 2 @tls_zeroed)
// CHECK: load i16
unsigned short read_tls(void) {
  return tls_initialized + tls_zeroed;
}
