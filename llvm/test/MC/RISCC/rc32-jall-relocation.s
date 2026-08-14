# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -mattr=+rc32 -filetype=obj < %s | llvm-readobj -r - | FileCheck %s

.text
jall s7, target

# CHECK: Relocations [
# CHECK: R_RISCC_JALL21 target 0x0
