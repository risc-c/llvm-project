# RUN: llvm-mc -triple=riscc-none-elf -mcpu=min -mattr=+jall -filetype=obj %s | llvm-readobj --file-headers - | FileCheck %s

jall s7, 0

# CHECK: Flags [
# CHECK: EF_RISCC_ABI_V0 (0x1)
# CHECK: EF_RISCC_PROFILE_SYS (0x30)
