# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+rc32 -disassemble < %s | FileCheck %s

0x74 0x38 0x00 0x00
# CHECK: jall s7, 65536
