// REQUIRES: riscc-registered-target
// RUN: %clang_cc1 -mrelocation-model static -triple riscc -target-cpu full -emit-obj -debug-info-kind=limited -dwarf-version=5 %s -o %t.rc16.o
// RUN: llvm-dwarfdump --debug-info --debug-line --debug-addr %t.rc16.o | FileCheck %s --check-prefix=RC16
// RUN: %clang_cc1 -mrelocation-model static -triple riscc -target-cpu full -target-feature +rc32 -emit-obj -debug-info-kind=limited -dwarf-version=5 %s -o %t.rc32.o
// RUN: llvm-dwarfdump --debug-info --debug-line --debug-addr %t.rc32.o | FileCheck %s --check-prefix=RC32

// RC16: Compile Unit: {{.*}}addr_size = 0x02
// RC16: address_size: 2
// RC16: Address table header: {{.*}}addr_size = 0x02
// RC32: Compile Unit: {{.*}}addr_size = 0x04
// RC32: address_size: 4
// RC32: Address table header: {{.*}}addr_size = 0x04

int global;
int read_global(void) { return global; }
