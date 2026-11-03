// REQUIRES: riscc-registered-target
// RUN: %clang --target=riscc-none-elf -mcpu=full -mrc32 -mmulhu -Os -S %s -o - | FileCheck %s --check-prefixes=MULHU,SOFTDIV
// RUN: %clang --target=riscc-none-elf -mcpu=full -mrc32 -mmulhu -mno-mulhu -Os -S %s -o - | FileCheck %s --check-prefix=BASE
// RUN: %clang --target=riscc-none-elf -mcpu=full -mrc32 -mmdu -mno-mulhu -Os -S %s -o - | FileCheck %s --check-prefix=BASE
// RUN: %clang --target=riscc-none-elf -mcpu=full -mrc32 -mno-mulhu -mmdu -Os -S %s -o - | FileCheck %s --check-prefixes=MULHU,MDU

// MULHU-LABEL: product:
// MULHU: mulhu
// MULHU: ret
// BASE-LABEL: product:
// BASE-NOT: mulhu
// BASE: __riscc_umulwide
unsigned long long product(unsigned a, unsigned b) {
  return (unsigned long long)a * b;
}

// MULHU-LABEL: constant_division:
// MULHU: mulhu
// MULHU: ret
// BASE-LABEL: constant_division:
// BASE-NOT: mulhu
// BASE: __udivsi3
unsigned constant_division(unsigned a) { return a / 26; }

// MULHU-LABEL: quotient:
// SOFTDIV-NOT: divu
// SOFTDIV: __udivsi3
// MDU: divu
// BASE-LABEL: quotient:
// BASE-NOT: divu
// BASE: __udivsi3
unsigned quotient(unsigned a, unsigned b) { return a / b; }
