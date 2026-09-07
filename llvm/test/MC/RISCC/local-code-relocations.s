# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj < %s | llvm-readobj -r - | FileCheck %s

# Keep code relocations for local labels so the linker checks alignment and code-address range.
.text
  .short code(local)
  ldi16 r1, code(local)
  jall s7, code(local)
local:
  nop

# CHECK: Relocations [
# CHECK: Section {{.*}} .rela.text {
# CHECK-NEXT: 0x0 R_RISCC_CODE16 .text 0xA
# CHECK-NEXT: 0x2 R_RISCC_CODE_HI8 .text 0xA
# CHECK-NEXT: 0x4 R_RISCC_CODE_LO8 .text 0xA
# CHECK-NEXT: 0x8 R_RISCC_CODE16 .text 0xA
# CHECK-NEXT: }
# CHECK-NEXT: ]
