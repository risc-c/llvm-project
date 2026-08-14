# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=min -mattr=+rc32 -show-encoding < %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=min -mattr=+rc32 -filetype=obj < %s | llvm-objdump --mattr=+rc32 -d - | FileCheck %s --check-prefix=DIS

ld r1, [r2 + -256]
# ENC: ld r1, [r2 + -256]{{ *}}; encoding: [0x02,0x4a]
st r7, [r0 + 252]
# ENC: st r7, [r0 + 252]{{ *}}; encoding: [0xfd,0x78]
ldi r3, 255
# ENC: ldi r3, 255{{ *}}; encoding: [0xff,0x98]
add r1, r2, r3
# ENC: add r1, r2, r3{{ *}}; encoding: [0x03,0xca]
sub r4, r5, r6
# ENC: sub r4, r5, r6{{ *}}; encoding: [0x0e,0xe5]
slt r0, r1, r2
# ENC: slt r0, r1, r2{{ *}}; encoding: [0x12,0xc1]
sltu r3, r4, r5
# ENC: sltu r3, r4, r5{{ *}}; encoding: [0x1d,0xdc]
ldx r6, [r7 + r0]
# ENC: ldx r6, [r7 + r0]{{ *}}; encoding: [0x40,0xf7]
ldb r1, [r2]
# ENC: ldb r1, [r2]{{ *}}; encoding: [0x50,0xca]
ldbs r3, [r4]
# ENC: ldbs r3, [r4]{{ *}}; encoding: [0x70,0xdc]
stb r5, [r6]
# ENC: stb r5, [r6]{{ *}}; encoding: [0x58,0xee]
ldh r7, [r0]
# ENC: ldh r7, [r0]{{ *}}; encoding: [0x52,0xf8]
ldhs r1, [r2]
# ENC: ldhs r1, [r2]{{ *}}; encoding: [0x72,0xca]
sth r3, [r4]
# ENC: sth r3, [r4]{{ *}}; encoding: [0x5a,0xdc]
ldpc r4, .Lliteral
# ENC: ldpc r4, .Lliteral
jmp8 .Ldone
.balign 4
.Lliteral:
.long 0
.Ldone:

# DIS: ld r1, [r2 + -256]
# DIS: st r7, [r0 + 252]
# DIS: ldh r7, [r0]
# DIS: ldhs r1, [r2]
# DIS: sth r3, [r4]
# DIS: ldpc r4,
