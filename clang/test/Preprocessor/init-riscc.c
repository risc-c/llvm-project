// RUN: %clang_cc1 -E -dM -ffreestanding -triple riscc-none-elf \
// RUN:   -target-cpu full < /dev/null | FileCheck %s

// CHECK-DAG: #define __RISCC_FULL__ 1
// CHECK-DAG: #define __RISCC__ 1
// CHECK-DAG: #define __riscc__ 1
// CHECK-DAG: #define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
// CHECK-DAG: #define __INT_MAX__ 32767
// CHECK-DAG: #define __INTPTR_TYPE__ int
// CHECK-DAG: #define __LONG_LONG_MAX__ 9223372036854775807LL
// CHECK-DAG: #define __LONG_MAX__ 2147483647L
// CHECK-DAG: #define __POINTER_WIDTH__ 16
// CHECK-DAG: #define __SIZEOF_INT__ 2
// CHECK-DAG: #define __SIZEOF_LONG_LONG__ 8
// CHECK-DAG: #define __SIZEOF_LONG__ 4
// CHECK-DAG: #define __SIZEOF_POINTER__ 2
// CHECK-DAG: #define __SIZE_TYPE__ unsigned int
