# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj < %s | llvm-objdump -d - | FileCheck %s

cmpi r0, 0
start:
beqz target
bnez start
bltz target
bgez start
jmp8 target
target:
nop

# CHECK: 0: 00 83         cmpi	r0, 0
# CHECK: 2: 08 87         beqz	.+10
# CHECK: 4: fd 8f         bnez	.-2
# CHECK: 6: 04 97         bltz	.+6
# CHECK: 8: f9 9f         bgez	.-6
# CHECK: a: 00 a7         jmp8	.+2
# CHECK: c: 28 c0         or	r0, r0, r0
