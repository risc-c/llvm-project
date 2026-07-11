# REQUIRES: riscc-registered-target
# RUN: split-file %s %t
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=null < %t/ranges.s 2>&1 | FileCheck %s --check-prefix=RANGE
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=null < %t/address.s 2>&1 | FileCheck %s --check-prefix=ADDRESS
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj -o /dev/null < %t/encoding.s 2>&1 | FileCheck %s --check-prefix=ENCODING
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj -o /dev/null < %t/far-branch.s 2>&1 | FileCheck %s --check-prefix=BRANCH

#--- ranges.s
ldi r0, 256
# RANGE: error: immediate must be in the range 0..255
addi r1, -129
# RANGE: error: immediate must be in the range -128..127
shri r2, r3, 0
# RANGE: error: shift amount must be in the range 1..8
shli r2, r3, 9
# RANGE: error: shift amount must be in the range 1..8

#--- address.s
ldw r0, [r1 + r2]
# ADDRESS: error: register-indexed word loads use LDWX

#--- encoding.s
jal16 s7, 3
# ENCODING: error: direct target is not a 15-bit word address
jal16 s7, 65536
# ENCODING: error: direct target is not a 15-bit word address
jal16 s7, lo8(func)
# ENCODING: error: only code() is valid on a direct control target
beqz code(func)
# ENCODING: error: target modifier is invalid on a short branch
li r0, lo8(data)
# ENCODING: error: LI accepts only an unmodified or code() expression

#--- far-branch.s
beqz far
# BRANCH: error: branch target out of signed 8-bit range
.space 514
far:
nop
