# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj < %s | llvm-readobj -h - | FileCheck %s --check-prefix=FULL
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=sys -filetype=obj < %s | llvm-readobj -h - | FileCheck %s --check-prefix=SYS
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=min -filetype=obj < %s | llvm-readobj -h - | FileCheck %s --check-prefix=MIN

nop

# FULL: Machine: EM_RISCC (0xC8C8)
# FULL: Flags [ (0x11)
# FULL-NEXT: EF_RISCC_ABI_V1 (0x1)
# FULL-NEXT: EF_RISCC_PROFILE_FULL (0x10)

# SYS: Machine: EM_RISCC (0xC8C8)
# SYS: Flags [ (0x31)
# SYS-NEXT: EF_RISCC_ABI_V1 (0x1)
# SYS-NEXT: EF_RISCC_PROFILE_SYS (0x30)

# MIN: Machine: EM_RISCC (0xC8C8)
# MIN: Flags [ (0x21)
# MIN-NEXT: EF_RISCC_ABI_V1 (0x1)
# MIN-NEXT: EF_RISCC_PROFILE_MIN (0x20)
