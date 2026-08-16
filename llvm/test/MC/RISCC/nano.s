# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=nano -show-encoding < %s | FileCheck %s
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=nano -filetype=obj < %s | \
# RUN:   llvm-objdump -d --mcpu=nano - | FileCheck %s --check-prefix=DIS

ld r1, [r2 + 126]
st r7, [r0 + -128]
ldx r0, [r1 + r2]
jalr r6, r2
jmp r4
jalr r6, r3

# CHECK: ld r1, [r2 + 126]
# CHECK-SAME: encoding: [0x7e,0x4a]
# CHECK: st r7, [r0 + -128]
# CHECK-SAME: encoding: [0x81,0x78]
# CHECK: ldx r0, [r1 + r2]
# CHECK-SAME: encoding: [0x42,0xc1]
# CHECK: jalr r6, r2
# CHECK-SAME: encoding: [0xf9,0xf2]
# CHECK: jmp r4
# CHECK-SAME: encoding: [0xf9,0xc4]
# CHECK: jalr r6, r3
# CHECK-SAME: encoding: [0xf9,0xf3]

# DIS: ld r1, [r2 + 126]
# DIS: st r7, [r0 + -128]
# DIS: ldx r0, [r1 + r2]
# DIS: jalr r6, r2
# DIS: jalr r0, r4
# DIS: jalr r6, r3
