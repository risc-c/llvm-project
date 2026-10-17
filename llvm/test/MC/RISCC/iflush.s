# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc -mcpu=sys -mattr=+iflush -show-encoding %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=riscc -mcpu=full -mattr=+iflush -show-encoding %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=riscc -mcpu=sys -mattr=+rc32,+iflush -show-encoding %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=riscc -mcpu=full -mattr=+rc32,+iflush -show-encoding %s | FileCheck %s --check-prefix=ENC
# RUN: not llvm-mc -triple=riscc -mcpu=nano -mattr=+sys,+iflush -filetype=obj %s -o %t.o 2>&1 | FileCheck %s --check-prefix=INVALID
# RUN: echo '0xf8 0xd8' | llvm-mc -triple=riscc -mcpu=nano -mattr=+sys,+iflush -disassemble 2>&1 | FileCheck %s --check-prefix=DECODE-INVALID

# Nano must not gain IFLUSH merely by enabling the mainline system feature.
iflush
# ENC: iflush{{ *}}; encoding: [0xf8,0xd8]
# INVALID: error: invalid operand for RISC-C instruction
# DECODE-INVALID: warning: invalid instruction encoding
