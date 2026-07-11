# REQUIRES: riscc-registered-target
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=min -filetype=null < %s 2>&1 | FileCheck %s --check-prefix=MIN
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=sys -filetype=null < %s 2>&1 | FileCheck %s --check-prefix=SYS

mul r1, r2, r3
# MIN: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction
# SYS: :[[@LINE-2]]:1: error: invalid operand for RISC-C instruction

shli r1, r2, 1
# MIN: :[[@LINE-1]]:1: error: instruction or shift count is unavailable in this profile

shri r1, r2, 2
# MIN: :[[@LINE-1]]:1: error: instruction or shift count is unavailable in this profile

sari r1, r2, 8
# MIN: :[[@LINE-1]]:1: error: instruction or shift count is unavailable in this profile

jal16 s7, 4
# MIN: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction

reti s7
# MIN: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction

cli
# MIN: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction

sti
# MIN: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction
