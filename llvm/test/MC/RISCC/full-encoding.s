# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -show-encoding < %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj < %s | llvm-objdump -d - | FileCheck %s --check-prefix=DIS

# Exercise every full-profile instruction format, the boundary registers, and
# the boundary immediates.  Keep these bytes in sync with the ISA assembler
# oracle in tools/riscc_asm.py.

ldw r1, [r2 + 126]
# ENC: ldw	r1, [r2 + 126]{{ *}}; encoding: [0x7e,0x4a]
stw r7, [r0 + -128]
# ENC: stw	r7, [r0 + -128]{{ *}}; encoding: [0x81,0x78]
ldi r0, 0
# ENC: ldi	r0, 0{{ *}}; encoding: [0x00,0x80]
lui r7, 255
# ENC: lui	r7, 255{{ *}}; encoding: [0xff,0xb9]
addi r1, -128
# ENC: addi	r1, -128{{ *}}; encoding: [0x80,0x8a]
cmpi r2, 127
# ENC: cmpi	r2, 127{{ *}}; encoding: [0x7f,0x93]
andi r3, 165
# ENC: andi	r3, 165{{ *}}; encoding: [0xa5,0x9c]
ori r4, 90
# ENC: ori	r4, 90{{ *}}; encoding: [0x5a,0xa5]
xori r5, 255
# ENC: xori	r5, 255{{ *}}; encoding: [0xff,0xae]

add r0, r1, r2
# ENC: add	r0, r1, r2{{ *}}; encoding: [0x02,0xc1]
sub r1, r2, r3
# ENC: sub	r1, r2, r3{{ *}}; encoding: [0x0b,0xca]
slt r2, r3, r4
# ENC: slt	r2, r3, r4{{ *}}; encoding: [0x14,0xd3]
sltu r3, r4, r5
# ENC: sltu	r3, r4, r5{{ *}}; encoding: [0x1d,0xdc]
and r4, r5, r6
# ENC: and	r4, r5, r6{{ *}}; encoding: [0x26,0xe5]
or r5, r6, r7
# ENC: or	r5, r6, r7{{ *}}; encoding: [0x2f,0xee]
xor r6, r7, r0
# ENC: xor	r6, r7, r0{{ *}}; encoding: [0x30,0xf7]
mul r7, r0, r1
# ENC: mul	r7, r0, r1{{ *}}; encoding: [0x39,0xf8]
fsl1 r5, r3, r2
# ENC: fsl1	r5, r3, r2{{ *}}; encoding: [0x9a,0xeb]
fsr1 r5, r3, r2
# ENC: fsr1	r5, r3, r2{{ *}}; encoding: [0x92,0xeb]
ldwx r0, [r1 + r2]
# ENC: ldwx	r0, [r1 + r2]{{ *}}; encoding: [0x42,0xc1]
ldb r1, [r2 + r3]
# ENC: ldb	r1, [r2 + r3]{{ *}}; encoding: [0x53,0xca]
ldbs r2, [r3 + r4]
# ENC: ldbs	r2, [r3 + r4]{{ *}}; encoding: [0x74,0xd3]
stb r3, [r4]
# ENC: stb	r3, [r4]{{ *}}; encoding: [0x58,0xdc]
srli r4, r5, 1
# ENC: srli	r4, r5, 1{{ *}}; encoding: [0x60,0xe5]
srai r5, r6, 4
# ENC: srai	r5, r6, 4{{ *}}; encoding: [0x6b,0xee]
slli r6, r7, 8
# ENC: slli	r6, r7, 8{{ *}}; encoding: [0x7f,0xf7]

ret s7
# ENC: ret	s7{{ *}}; encoding: [0xf8,0xc7]
jalr s6, r5
# ENC: jalr	s6, r5{{ *}}; encoding: [0xf9,0xf5]
mfs r4, s3
# ENC: mfs	r4, s3{{ *}}; encoding: [0xfa,0xe3]
mts s2, r1
# ENC: mts	s2, r1{{ *}}; encoding: [0xfb,0xd1]
reti s0
# ENC: reti	s0{{ *}}; encoding: [0xf8,0xf8]
jal16 s7, 4660
# ENC: jal16	s7, 4660{{ *}}; encoding: [0x00,0x3f,0x1a,0x09]
cli
# ENC: cli{{ *}}; encoding: [0xfe,0xc0]
sti
# ENC: sti{{ *}}; encoding: [0xfe,0xf8]

li r0, 4660
# ENC: li	r0, 4660{{ *}}; encoding: [0x12,0x81,0x34,0x85]
ldi16 r0, 4660
# ENC: ldi16	r0, 4660{{ *}}; encoding: [0x12,0x81,0x34,0x85]
call r1
# ENC: call	r1{{ *}}; encoding: [0xf9,0xf9]
call16 4660
# ENC: call16	4660{{ *}}; encoding: [0x00,0x3f,0x1a,0x09]
jmp16 4660
# ENC: jmp16	4660{{ *}}; encoding: [0x00,0x07,0x1a,0x09]
rets
# ENC: rets{{ *}}; encoding: [0xf8,0xc7]
mov r2, r3
# ENC: mov	r2, r3{{ *}}; encoding: [0x2b,0xd3]
nop
# ENC: nop{{ *}}; encoding: [0x28,0xc0]
halt
# ENC: halt{{ *}}; encoding: [0xff,0xa7]

# Spot-check that the emitted object can be decoded, including the two-word
# long formats.  Pseudos intentionally disassemble to canonical instructions.
# DIS: ldw	r1, [r2 + 126]
# DIS: mul	r7, r0, r1
# DIS: fsl1	r5, r3, r2
# DIS: fsr1	r5, r3, r2
# DIS: reti	s0
# DIS: jal16	s7, 4660
# DIS: cli
# DIS: sti
# DIS: lui	r0, 18
# DIS: ori	r0, 52
# DIS: lui	r0, 18
# DIS: ori	r0, 52
# DIS: jal16	s7, 4660
# DIS: jal16	s0, 4660
# DIS: ret	s7
