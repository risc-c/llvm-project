# RUN: llvm-mc -triple=riscc-none-elf -mcpu=sys -filetype=obj %s | llvm-readobj --relocations - | FileCheck %s

        .text
        add     r1, r1, r1
        .reloc . - 2, R_RISCC_NONE, target
        .reloc . - 2, BFD_RELOC_NONE, target
        .cg_profile caller, target, 100

        .section .text.target,"ax",@progbits
target:
        ret     s7

# CHECK: Relocations [
# CHECK: R_RISCC_NONE target 0x0
# CHECK: R_RISCC_NONE target 0x0
# CHECK: .rel.llvm.call-graph-profile
# CHECK: R_RISCC_NONE caller
# CHECK: R_RISCC_NONE target
