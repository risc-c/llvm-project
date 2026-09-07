# REQUIRES: riscc
# RUN: llvm-mc -triple=riscc -mcpu=min -mattr=+rc32 -filetype=obj %s -o %t.o
# RUN: ld.lld -m elf32lriscc -e 0 --defsym=high=0x80000000 --defsym=ones=0xffffffff %t.o -o %t
# RUN: llvm-readobj --hex-dump=.data %t | FileCheck %s --check-prefix=HIGH
# RUN: ld.lld -m elf32lriscc -e 0 --defsym=high=0xfffffffe --defsym=ones=0xffffffff %t.o -o %t
# RUN: llvm-readobj --hex-dump=.data %t | FileCheck %s --check-prefix=MAX

# HIGH: 00000080 ffffffff 00000080
# MAX: feffffff ffffffff feffffff

.data
.long high
.long ones
.long call_target(high)
