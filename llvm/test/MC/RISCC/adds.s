# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+adds -show-encoding < %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+rc32,+adds -filetype=obj < %s | llvm-objdump -d --mattr=+rc32,+adds - | FileCheck %s --check-prefix=DIS
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full < %s 2>&1 | FileCheck %s --check-prefix=DISABLED
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=nano -mattr=+adds < %s 2>&1 | FileCheck %s --check-prefix=DISABLED

adds r3, s2, r1
# ENC: adds r3, s2, r1{{ *}}; encoding: [0xc1,0xda]
# DIS: adds r3, s2, r1

adds r5, s7, r5
# ENC: adds r5, s7, r5{{ *}}; encoding: [0xc5,0xef]
# DIS: adds r5, s7, r5
# DISABLED: error: invalid operand for RISC-C instruction
