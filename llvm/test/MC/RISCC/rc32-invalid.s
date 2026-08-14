# REQUIRES: riscc-registered-target
# RUN: split-file %s %t
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=min -filetype=null < %t/rc16.s 2>&1 | FileCheck %s --check-prefix=RC16
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=min -mattr=+rc32 -filetype=null < %t/rc32.s 2>&1 | FileCheck %s --check-prefix=RC32

#--- rc16.s
ldpc r1, 0
# RC16: error: invalid operand for RISC-C instruction
ldh r2, [r3]
# RC16: error: invalid operand for RISC-C instruction

#--- rc32.s
lui r1, 0
# RC32: error: invalid operand for RISC-C instruction
ld r2, [r3 + 2]
# RC32: error: invalid operand for RISC-C instruction
