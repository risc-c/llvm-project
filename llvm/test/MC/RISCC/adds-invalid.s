# REQUIRES: riscc-registered-target
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+adds < %s 2>&1 | FileCheck %s
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+rc32,+adds < %s 2>&1 | FileCheck %s

# The first source is an S-register and the second is an ordinary register.
adds r3, r1, s2
# CHECK: error: invalid operand for RISC-C instruction
adds r3, r1, r2
# CHECK: error: invalid operand for RISC-C instruction
adds r3, s1, s2
# CHECK: error: invalid operand for RISC-C instruction
