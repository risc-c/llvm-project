# REQUIRES: riscc-registered-target
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full < %s 2>&1 | FileCheck %s --check-prefix=DISABLED
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=nano -mattr=+vsh < %s 2>&1 | FileCheck %s --check-prefix=NANO

sll r1, r2

# DISABLED: error: invalid operand for RISC-C instruction
# NANO: error: invalid operand for RISC-C instruction
