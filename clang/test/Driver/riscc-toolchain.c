// RUN: %clang -### --target=riscc-none-elf -mcpu=full -fuse-ld=lld \
// RUN:   -nostdlib %s 2>&1 | FileCheck %s

// CHECK: "-cc1" "-triple" "riscc-unknown-none-elf"
// CHECK-SAME: "-target-cpu" "full"
// CHECK: "{{.*}}ld.lld{{(.exe)?}}"
// CHECK-SAME: "-Bstatic"
// CHECK-SAME: "-m" "elf32lriscc"
