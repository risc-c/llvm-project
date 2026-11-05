# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+vsh -show-encoding < %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+rc32,+vsh -filetype=obj < %s | llvm-objdump -d --mattr=+rc32,+vsh - | FileCheck %s --check-prefix=DIS

sll r3, r1
# ENC: sll r3, r1{{ *}}; encoding: [0x8a,0xd9]
srl r4, r2
# ENC: srl r4, r2{{ *}}; encoding: [0x8b,0xe2]
sra r5, r6
# ENC: sra r5, r6{{ *}}; encoding: [0x8f,0xee]

# DIS: sll r3, r1
# DIS: srl r4, r2
# DIS: sra r5, r6
