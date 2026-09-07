# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=nano -disassemble < %s 2>&1 | FileCheck %s

# Byte-load subopcode 011 is reserved.
0x53 0xc0
# CHECK: warning: invalid instruction encoding
