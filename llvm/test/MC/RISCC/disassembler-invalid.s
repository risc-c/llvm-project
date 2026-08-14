# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -disassemble < %s 2>&1 | FileCheck %s --check-prefix=FULL
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=min -disassemble < %s 2>&1 | FileCheck %s --check-prefix=MIN

# Return-control selector 001 is reserved.
0xf8 0xc8
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# The former control-group selector is reserved.
0xfc 0xc0
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# The former CLI/STI sub-op is reserved.
0xfe 0xc8
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding
0xfe 0xc1
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# The former STI selector is reserved.
0xff 0xc0
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# The old LDPH fffff=01_001 slot is reserved, including encodings whose aaa
# field the former decoder ignored.
0x4f 0xc0
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding
0x48 0xc1
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# Direct data-memory operations reserve their other low-register selectors.
0x51 0xc0
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding
0x59 0xc0
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding
0x71 0xc0
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# The compact two-operand group reserves ooo=101..111.
0x8d 0xeb
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# The former three-register FSL1 and FSR1 f5 slots are reserved.
0x9a 0xeb
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding
0x92 0xeb
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# Branch condition 5 is unassigned.
0x00 0xaf
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# Long opcode 010 is reserved.
0x00 0x02 0x00 0x00
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# The former LDI16 long opcode 000 is reserved.
0x00 0x00 0x34 0x12
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# The unused long-head byte must be zero.
0x01 0x00 0x34 0x12
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# RC16 reserves the JALL high address bits.
0x74 0x38 0x00 0x00
# FULL: warning: invalid instruction encoding
# MIN: warning: invalid instruction encoding

# A JALL payload is an aligned 16-bit byte address.
0x34 0x00 0x00 0x80
# FULL: jall s0, 32768
# MIN: warning: invalid instruction encoding

# MUL is structurally valid but unavailable in min.
0x39 0xc1
# FULL: mul r0, r1, r1
# MIN: warning: invalid instruction encoding

# RETI, CLI, and STI are structurally valid but unavailable in min.
0xf8 0xe8
# FULL: reti s0
# MIN: warning: invalid instruction encoding
0xf8 0xd0
# FULL: cli
# MIN: warning: invalid instruction encoding
0xf8 0xf8
# FULL: sti
# MIN: warning: invalid instruction encoding
