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
// RUN: %clang -### --target=riscc-none-elf -mcpu=min -mrc32 -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=RC32
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mno-mdu -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=NO-MDU
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mmulhu -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=MULHU --implicit-check-not='"+mdu"'
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mmulhu -mno-mulhu -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=NO-MULHU --implicit-check-not='"+mulhu"'
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mno-mulhu -mmulhu -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=MULHU --implicit-check-not='"-mulhu"'
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -madds -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=ADDS --implicit-check-not='"-adds"'
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -madds -mno-adds -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=NO-ADDS --implicit-check-not='"+adds"'
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mno-adds -madds -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=ADDS --implicit-check-not='"-adds"'
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mvsh -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=VSH --implicit-check-not='"-vsh"'
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mvsh -mno-vsh -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=NO-VSH --implicit-check-not='"+vsh"'
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mno-vsh -mvsh -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=VSH --implicit-check-not='"-vsh"'
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mmdu -mno-mulhu -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=MDU-NO-MULHU
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mno-mulhu -mmdu -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=NO-MULHU-MDU
// RUN: %clang -### --target=riscc-none-elf -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=DEFAULT --implicit-check-not='"+adds"' --implicit-check-not='"+vsh"'
// RUN: not %clang --target=riscc-none-elf -mcpu=invalid -c %s -o %t.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=INVALID-CPU
// RUN: not %clang --target=riscc-none-elf -mcpu=min -mmulhu -c %s -o %t.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=INVALID-MULHU
// RUN: not %clang --target=riscc-none-elf -mcpu=nano -mrc32 -c %s -o %t.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=RC32-NANO
// RUN: not %clang --target=riscc-none-elf -mcpu=min -mrc32x -c %s -o %t.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=RC32X
// RUN: %clang -### --target=riscc-none-elf -mcpu=full -mrc32 -mmdu -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=RC32-MDU
// RUN: not %clang -### --target=riscc-none-elf -mrc32 -mcmodel=small -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=CODE-MODEL

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

// MULHU: "-cc1"
// MULHU-SAME: "-target-feature" "+mulhu"
// NO-MULHU: "-cc1"
// NO-MULHU-SAME: "-target-feature" "-mulhu"
// ADDS: "-cc1"
// ADDS-SAME: "-target-feature" "+adds"
// NO-ADDS: "-cc1"
// NO-ADDS-SAME: "-target-feature" "-adds"
// VSH: "-cc1"
// VSH-SAME: "-target-feature" "+vsh"
// NO-VSH: "-cc1"
// NO-VSH-SAME: "-target-feature" "-vsh"
// MDU-NO-MULHU: "-cc1"
// MDU-NO-MULHU-SAME: "-target-feature" "+mdu"
// MDU-NO-MULHU-SAME: "-target-feature" "-mulhu"
// NO-MULHU-MDU: "-cc1"
// NO-MULHU-MDU-SAME: "-target-feature" "-mulhu"
// NO-MULHU-MDU-SAME: "-target-feature" "+mdu"
// INVALID-MULHU: error: invalid feature combination: mulhu requires -mcpu=full

// RC32: "-target-cpu" "min"
// RC32-SAME: "-target-feature" "+rc32"

// DEFAULT: "-cc1"
// DEFAULT-SAME: "-target-cpu" "full"

// INVALID-CPU: error: unknown target CPU 'invalid'
// INVALID-CPU-NEXT: note: valid target CPU values are: nano, min, sys, full
// RC32-NANO: error: invalid feature combination: rc32 has no Nano profile
// RC32X: error: invalid feature combination: rc32x is not implemented
// RC32-MDU: "-target-feature" "+mdu"
// RC32-MDU-SAME: "-target-feature" "+rc32"
// CODE-MODEL: error: unsupported argument 'small' to option '-mcmodel=' for target 'riscc-unknown-none-elf'
