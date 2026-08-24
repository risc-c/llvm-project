# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+rc32 -disassemble < %s 2>&1 | FileCheck %s --check-prefix=FULL
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=sys -mattr=+rc32 -disassemble < %s 2>&1 | FileCheck %s --check-prefix=BASE
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=min -mattr=+rc32 -disassemble < %s 2>&1 | FileCheck %s --check-prefix=BASE

# Immediate left shift is Full-only.
0x78 0xc1
# FULL: slli r0, r1, 1
# BASE-DAG: warning: invalid instruction encoding

# Count-one right shifts are available in every RC32 profile.
0x60 0xc1
# FULL: srli r0, r1, 1
# BASE-DAG: srli r0, r1, 1
0x68 0xc1
# FULL: srai r0, r1, 1
# BASE-DAG: srai r0, r1, 1

# Larger right-shift counts are Full-only.
0x61 0xc1
# FULL: srli r0, r1, 2
# BASE-DAG: warning: invalid instruction encoding
0x6f 0xc1
# FULL: srai r0, r1, 8
# BASE-DAG: warning: invalid instruction encoding
