# RUN: llvm-mc -triple=riscc-none-elf -mcpu=min -mattr=+wide-shift -filetype=obj %s | llvm-readobj --file-headers - | FileCheck %s

slli r1, r1, 2

# CHECK: Flags [
# CHECK: EF_RISCC_ABI_V0 (0x1)
# CHECK: EF_RISCC_PROFILE_FULL (0x10)
