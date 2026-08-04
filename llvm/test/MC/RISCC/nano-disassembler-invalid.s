# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=nano -disassemble < %s 2>&1 | FileCheck %s

# LDPH is a mandatory mainline instruction but is not part of Nano.
0x53 0xc0
# CHECK: warning: invalid instruction encoding
