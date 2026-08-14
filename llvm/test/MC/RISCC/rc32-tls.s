# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=min -mattr=+rc32 -filetype=obj < %s | llvm-readobj -r - | FileCheck %s

.text
.long tpoff(tls)

.section .tbss,"awT",@nobits
.globl tls
.p2align 2
tls:
.long 0

# CHECK: Relocations [
# CHECK: R_RISCC_TPOFF32 tls 0x0
