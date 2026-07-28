// RUN: %clang -### --target=riscc-none-elf -mcpu=full -fuse-ld=lld \
// RUN:   -nostdlib %s 2>&1 | FileCheck %s
// RUN: %clang -### --target=riscc-none-elf -mcpu=min -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=MIN
// RUN: %clang -### --target=riscc-none-elf -mcpu=sys -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=SYS
// RUN: %clang -### --target=riscc-none-elf -mcpu=nano -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=NANO
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mmdu -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=MDU
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mno-mdu -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=NO-MDU
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

// NANO: "-cc1"
// NANO-SAME: "-target-cpu" "nano"

// MDU: "-cc1"
// MDU-SAME: "-target-cpu" "full"
// MDU-SAME: "-target-feature" "+mdu"

// NO-MDU: "-cc1"
// NO-MDU-SAME: "-target-cpu" "full"
// NO-MDU-SAME: "-target-feature" "-mdu"

// DEFAULT: "-cc1"
// DEFAULT-SAME: "-target-cpu" "full"

// INVALID-CPU: error: unknown target CPU 'invalid'
// INVALID-CPU-NEXT: note: valid target CPU values are: nano, min, sys, full
