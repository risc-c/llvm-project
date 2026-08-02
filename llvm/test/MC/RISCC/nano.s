# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=nano -show-encoding < %s | FileCheck %s
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=nano -filetype=obj < %s | \
# RUN:   llvm-objdump -d --mcpu=nano - | FileCheck %s --check-prefix=DIS

jalr r6, r2
ret r4
call r3

# CHECK: jalr r6, r2
# CHECK-SAME: encoding: [0xf9,0xf2]
# CHECK: ret r4
# CHECK-SAME: encoding: [0xf9,0xc4]
# CHECK: call r3
# CHECK-SAME: encoding: [0xf9,0xf3]

# DIS: jalr r6, r2
# DIS: jalr r0, r4
# DIS: jalr r6, r3
