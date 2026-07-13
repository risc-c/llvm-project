# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -disassemble < %s 2>&1 | FileCheck %s --check-prefix=FULL
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=min -disassemble < %s 2>&1 | FileCheck %s --check-prefix=MIN

# Return-control selector 001 is reserved.
0xf8 0xc8
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# The old RETI system sub-op is reserved.
0xfc 0xc0
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# CLI/STI reserve the other selectors and the source field.
0xfe 0xc8
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding
0xfe 0xc1
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# The old STI system sub-op is reserved.
0xff 0xc0
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# STB reserves its low register field.
0x59 0xc0
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# RR function 9 is unassigned.
0x48 0xc0
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# Branch condition 5 is unassigned.
0x00 0xaf
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# JAL16 reserves the A field, even in a profile which implements JAL16.
0xfd 0xc1 0x00 0x00
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# A JAL16 payload is a 15-bit word index.
0xfd 0xc0 0x00 0x80
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# MUL is structurally valid but unavailable in min.
0x39 0xc1
# FULL: mul r0, r1, r1
# MIN: warning: invalid instruction encoding

# RETI, CLI, and STI are structurally valid but unavailable in min.
0xf8 0xf8
# FULL: reti s0
# MIN: warning: invalid instruction encoding
0xfe 0xc0
# FULL: cli
# MIN: warning: invalid instruction encoding
0xfe 0xf8
# FULL: sti
# MIN: warning: invalid instruction encoding
