# REQUIRES: riscc-registered-target
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=min -filetype=null < %s 2>&1 | FileCheck %s --check-prefix=MIN
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=sys -filetype=null < %s 2>&1 | FileCheck %s --check-prefix=SYS
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=nano -filetype=null < %s 2>&1 | FileCheck %s --check-prefix=NANO
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=min -mattr=+rc32 -filetype=null < %s 2>&1 | FileCheck %s --check-prefix=RC32-MIN
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=sys -mattr=+rc32 -filetype=null < %s 2>&1 | FileCheck %s --check-prefix=RC32-SYS

mul r1, r2, r3
# MIN: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction
# SYS: :[[@LINE-2]]:1: error: invalid operand for RISC-C instruction
# NANO: :[[@LINE-3]]:1: error: invalid operand for RISC-C instruction

fsl1 r1, r2
# NANO: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction

fsr1 r1, r2
# NANO: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction

slli r1, r2, 1
# MIN: :[[@LINE-1]]:1: error: instruction or shift count is unavailable in this profile
# SYS: :[[@LINE-2]]:1: error: instruction or shift count is unavailable in this profile
# NANO: :[[@LINE-3]]:1: error: invalid operand for RISC-C instruction
# RC32-MIN: :[[@LINE-4]]:1: error: instruction or shift count is unavailable in this profile
# RC32-SYS: :[[@LINE-5]]:1: error: instruction or shift count is unavailable in this profile

srli r1, r2, 2
# MIN: :[[@LINE-1]]:1: error: instruction or shift count is unavailable in this profile
# SYS: :[[@LINE-2]]:1: error: instruction or shift count is unavailable in this profile
# NANO: :[[@LINE-3]]:1: error: instruction or shift count is unavailable in this profile
# RC32-MIN: :[[@LINE-4]]:1: error: instruction or shift count is unavailable in this profile
# RC32-SYS: :[[@LINE-5]]:1: error: instruction or shift count is unavailable in this profile

srai r1, r2, 8
# MIN: :[[@LINE-1]]:1: error: instruction or shift count is unavailable in this profile
# SYS: :[[@LINE-2]]:1: error: instruction or shift count is unavailable in this profile
# NANO: :[[@LINE-3]]:1: error: instruction or shift count is unavailable in this profile
# RC32-MIN: :[[@LINE-4]]:1: error: instruction or shift count is unavailable in this profile
# RC32-SYS: :[[@LINE-5]]:1: error: instruction or shift count is unavailable in this profile

jall s7, 4
# MIN: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction
# NANO: :[[@LINE-2]]:1: error: invalid operand for RISC-C instruction

reti s7
# MIN: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction
# NANO: :[[@LINE-2]]:1: error: invalid operand for RISC-C instruction

cli
# MIN: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction
# NANO: :[[@LINE-2]]:1: error: invalid operand for RISC-C instruction

sti
# MIN: :[[@LINE-1]]:1: error: invalid operand for RISC-C instruction
# NANO: :[[@LINE-2]]:1: error: invalid operand for RISC-C instruction
