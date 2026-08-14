// RUN: %clang_cc1 -E -dM -ffreestanding -triple riscc-none-elf \
// RUN:   -target-cpu min < /dev/null | FileCheck %s \
// RUN:   --check-prefixes=COMMON,MIN --implicit-check-not=__RISCC_SYS__ \
// RUN:   --implicit-check-not=__RISCC_FULL__ --implicit-check-not=__RISCC_NANO__
// RUN: %clang_cc1 -E -dM -ffreestanding -triple riscc-none-elf \
// RUN:   -target-cpu nano < /dev/null | FileCheck %s \
// RUN:   --check-prefixes=COMMON,NANO --implicit-check-not=__RISCC_MIN__ \
// RUN:   --implicit-check-not=__RISCC_SYS__ --implicit-check-not=__RISCC_FULL__
// RUN: %clang_cc1 -E -dM -ffreestanding -triple riscc-none-elf \
// RUN:   -target-cpu sys < /dev/null | FileCheck %s \
// RUN:   --check-prefixes=COMMON,SYS --implicit-check-not=__RISCC_MIN__ \
// RUN:   --implicit-check-not=__RISCC_FULL__
// RUN: %clang_cc1 -E -dM -ffreestanding -triple riscc-none-elf \
// RUN:   -target-cpu full < /dev/null | FileCheck %s \
// RUN:   --check-prefixes=COMMON,FULL --implicit-check-not=__RISCC_MIN__ \
// RUN:   --implicit-check-not=__RISCC_SYS__ --implicit-check-not=__RISCC_MDU__ \
// RUN:   --implicit-check-not=__RISCC_MULHU__ --implicit-check-not=__RISCC_DIVU__
// RUN: %clang_cc1 -E -dM -ffreestanding -triple riscc-none-elf \
// RUN:   -target-cpu full -target-feature +mdu < /dev/null | FileCheck %s \
// RUN:   --check-prefixes=COMMON,FULL,MDU --implicit-check-not=__RISCC_MIN__ \
// RUN:   --implicit-check-not=__RISCC_SYS__
// RUN: %clang_cc1 -E -dM -ffreestanding -triple riscc-none-elf \
// RUN:   -target-cpu min -target-feature +rc32 < /dev/null | FileCheck %s \
// RUN:   --check-prefix=RC32

// COMMON-DAG: #define __RISCC__ 1
// COMMON-DAG: #define __riscc__ 1
// COMMON-DAG: #define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
// COMMON-DAG: #define __INT_MAX__ 32767
// COMMON-DAG: #define __INTPTR_TYPE__ int
// COMMON-DAG: #define __LONG_LONG_MAX__ 9223372036854775807LL
// COMMON-DAG: #define __LONG_MAX__ 2147483647L
// COMMON-DAG: #define __POINTER_WIDTH__ 16
// COMMON-DAG: #define __SIZEOF_INT__ 2
// COMMON-DAG: #define __SIZEOF_LONG_LONG__ 8
// COMMON-DAG: #define __SIZEOF_LONG__ 4
// COMMON-DAG: #define __SIZEOF_POINTER__ 2
// COMMON-DAG: #define __SIZE_TYPE__ unsigned int
// MIN-DAG: #define __RISCC_MIN__ 1
// NANO-DAG: #define __RISCC_NANO__ 1
// SYS-DAG: #define __RISCC_SYS__ 1
// FULL-DAG: #define __RISCC_FULL__ 1
// FULL-DAG: #define __RISCC_MUL__ 1
// MDU-DAG: #define __RISCC_MDU__ 1
// MDU-DAG: #define __RISCC_MULHU__ 1
// MDU-DAG: #define __RISCC_DIVU__ 1
// RC32-DAG: #define __RISCC_RC32__ 1
// RC32-DAG: #define __RISCC_XLEN__ 32
// RC32-DAG: #define __POINTER_WIDTH__ 32
// RC32-DAG: #define __SIZEOF_INT__ 4
// RC32-DAG: #define __SIZEOF_LONG__ 4
// RC32-DAG: #define __SIZEOF_POINTER__ 4
