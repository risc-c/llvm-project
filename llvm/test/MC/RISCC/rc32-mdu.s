# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+rc32,+mdu -show-encoding < %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+rc32,+mdu -filetype=obj < %s | llvm-objdump -d --mattr=+rc32,+mdu - | FileCheck %s --check-prefix=DIS

mulhu r3, r1, r2
# ENC: mulhu	r3, r1, r2{{ *}}; encoding: [0xa2,0xd9]
# DIS: mulhu	r3, r1, r2

divu r3, r1, r2
# ENC: divu	r3, r1, r2{{ *}}; encoding: [0x82,0xd9]
# DIS: divu	r3, r1, r2
