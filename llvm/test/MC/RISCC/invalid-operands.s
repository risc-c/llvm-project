# REQUIRES: riscc-registered-target
# RUN: split-file %s %t
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=null < %t/ranges.s 2>&1 | FileCheck %s --check-prefix=RANGE
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=null < %t/address.s 2>&1 | FileCheck %s --check-prefix=ADDRESS
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj -o /dev/null < %t/encoding.s 2>&1 | FileCheck %s --check-prefix=ENCODING
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj -o /dev/null < %t/far-branch.s 2>&1 | FileCheck %s --check-prefix=BRANCH
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj -o /dev/null < %t/alignment.s 2>&1 | FileCheck %s --check-prefix=ALIGN
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=null < %t/funnel.s 2>&1 | FileCheck %s --check-prefix=FUNNEL
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=null < %t/removed-pseudos.s 2>&1 | FileCheck %s --check-prefix=REMOVED
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=nano -filetype=null < %t/removed-nano-ret.s 2>&1 | FileCheck %s --check-prefix=REMOVED-NANO
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+rc32 -show-encoding < %t/rc32-jall.s | FileCheck %s --check-prefix=RC32-JALL
# RUN: not llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+rc32 -filetype=obj -o /dev/null < %t/rc32-ldi16.s 2>&1 | FileCheck %s --check-prefix=RC32-LDI16

#--- ranges.s
ldi r0, 256
# RANGE: error: immediate must be in the range 0..255
addi r1, -129
# RANGE: error: immediate must be in the range -128..127
srli r2, r3, 0
# RANGE: error: shift amount must be in the range 1..8
slli r2, r3, 9
# RANGE: error: shift amount must be in the range 1..8

#--- address.s
ld r0, [r1 + r2]
# ADDRESS: error: register-indexed word loads use LDX
ldx r0, [r1]
# ADDRESS: error: LDX address requires two registers
ldb r0, [r1 + r2]
# ADDRESS: error: direct address requires a single register
ldbs r0, [r1 + r2]
# ADDRESS: error: direct address requires a single register

#--- encoding.s
jall s7, 3
# ENCODING: error: direct target is not an aligned JALL byte address
jall s7, 65536
# ENCODING: error: direct target is not an aligned JALL byte address
jall s7, lo8(func)
# ENCODING: error: only code() is valid on a direct control target
beqz code(func)
# ENCODING: error: target modifier is invalid on a short branch
ldi16 r0, lo8(data)
# ENCODING: error: LDI16 accepts only an unmodified, code(), or tpoff() expression

#--- far-branch.s
beqz far
# BRANCH: error: branch target out of signed 8-bit range
.space 514
far:
nop

#--- alignment.s
.byte 0
nop
# ALIGN: error: instruction must be 2-byte aligned

#--- funnel.s
fsl1 r1, r2, r3
# FUNNEL: error: invalid operand for RISC-C instruction
fsl1 r1
# FUNNEL: error: invalid operand for RISC-C instruction
fsr1 r1, r2, r3
# FUNNEL: error: invalid operand for RISC-C instruction
fsr1 r1
# FUNNEL: error: invalid operand for RISC-C instruction

#--- removed-pseudos.s
li r0, 1
call r0
call16 0
tail r0
rets
ret r6
# REMOVED-COUNT-5: error: invalid RISC-C instruction mnemonic
# REMOVED: error: invalid operand for RISC-C instruction

#--- removed-nano-ret.s
ret r6
# REMOVED-NANO: error: invalid operand for RISC-C instruction

#--- rc32-jall.s
jall s7, 65536
# RC32-JALL: jall	s7, 65536{{ *}}; encoding: [0x74,0x38,0x00,0x00]
jall s7, 2097150
# RC32-JALL: jall	s7, 2097150{{ *}}; encoding: [0xf4,0x3f,0xfe,0xff]

#--- rc32-ldi16.s
ldi16 r0, 1
# RC32-LDI16: error: LDI16 is unavailable in RC32
