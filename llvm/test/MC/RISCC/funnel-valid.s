# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=min -filetype=null < %s
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=sys -filetype=null < %s
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=null < %s

fsl1 r1, r2
fsr1 r4, r5
