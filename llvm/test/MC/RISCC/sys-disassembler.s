# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -disassemble < %s 2>&1 | FileCheck %s --check-prefix=FULL
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=sys -disassemble < %s 2>&1 | FileCheck %s --check-prefix=SYS

# Sys and Full both consume the two-word JALL encoding.
0x34 0x38 0x04 0x00
# FULL: jall s7, 4
# SYS-DAG: jall s7, 4

# Count-two immediate right shift is Full-only.
0x61 0xc1
# FULL: srli r0, r1, 2
# SYS-DAG: warning: invalid instruction encoding

# System interrupt control remains valid in Sys.
0xf8 0xe8
# FULL: reti s0
# SYS: reti s0
