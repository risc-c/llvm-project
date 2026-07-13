// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s

#include <stdarg.h>

typedef struct {
  unsigned char byte[3];
} bytes3_t;

// CHECK-LABEL: define{{.*}} i16 @take_one(i16 noundef %fixed, ...)
// CHECK: call addrspace(1) void @llvm.va_start
// CHECK: call addrspace(1) void @llvm.va_copy
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i16 2
// CHECK-COUNT-2: call addrspace(1) void @llvm.va_end
unsigned int take_one(unsigned int fixed, ...) {
  va_list ap;
  va_list copy;
  unsigned int value;

  va_start(ap, fixed);
  va_copy(copy, ap);
  value = va_arg(ap, unsigned int);
  va_end(copy);
  va_end(ap);
  return value;
}

// CHECK-LABEL: define{{.*}} i16 @call_take_one()
// CHECK: call addrspace(1) i16 (i16, ...) @take_one(i16 noundef 1, i16 noundef 2)
unsigned int call_take_one(void) {
  return take_one(1, 2);
}

// CHECK-LABEL: define{{.*}} i16 @take_bytes3(i16 noundef %fixed, ...)
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i16 4
unsigned int take_bytes3(unsigned int fixed, ...) {
  va_list ap;
  bytes3_t value;

  va_start(ap, fixed);
  value = va_arg(ap, bytes3_t);
  va_end(ap);
  return value.byte[0];
}
