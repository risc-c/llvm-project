// RUN: %clang -### --target=riscc-none-elf -mcpu=full -fuse-ld=lld \
// RUN:   -nostdlib %s 2>&1 | FileCheck %s
// RUN: %clang -### --target=riscc-none-elf -mcpu=min -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=MIN
// RUN: %clang -### --target=riscc-none-elf -mcpu=sys -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=SYS
// RUN: %clang -### --target=riscc-none-elf -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=DEFAULT
// RUN: not %clang --target=riscc-none-elf -mcpu=invalid -c %s -o %t.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=INVALID-CPU

// CHECK: "-cc1" "-triple" "riscc-unknown-none-elf"
// CHECK-SAME: "-target-cpu" "full"
// CHECK: "{{.*}}ld.lld{{(.exe)?}}"
// CHECK-SAME: "-Bstatic"
// CHECK-SAME: "-m" "elf32lriscc"

// MIN: "-cc1"
// MIN-SAME: "-target-cpu" "min"

// SYS: "-cc1"
// SYS-SAME: "-target-cpu" "sys"

// DEFAULT: "-cc1"
// DEFAULT-SAME: "-target-cpu" "full"

// INVALID-CPU: error: unknown target CPU 'invalid'
// INVALID-CPU-NEXT: note: valid target CPU values are: min, sys, full
