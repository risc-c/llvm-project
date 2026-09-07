# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc -mcpu=full -filetype=obj %s -o %t.rc16.o
# RUN: llvm-dwarfdump --debug-frame %t.rc16.o | FileCheck %s --check-prefix=RC16
# RUN: llvm-mc -triple=riscc -mcpu=full -mattr=+rc32 -filetype=obj %s -o %t.rc32.o
# RUN: llvm-dwarfdump --debug-frame %t.rc32.o | FileCheck %s --check-prefix=RC32

# RC16: Address size: 2
# RC16: Data alignment factor: -2
# RC16: DW_CFA_offset: S7 -4
# RC32: Address size: 4
# RC32: Data alignment factor: -4
# RC32: DW_CFA_offset: S7 -4

.cfi_sections .debug_frame
f:
.cfi_startproc
.cfi_def_cfa 7, 4
.cfi_offset 15, -4
nop
.cfi_endproc
