# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj < %s | llvm-readobj -r - | FileCheck %s

.text
.globl caller
caller:
  ldi r0, lo8(data)
  lui r1, hi8(data)
  ldi16 r2, data
  ldi16 r3, code(func)
  jall s7, code(func)
  beqz external_target
  .byte data
  .short data
  .long data
  .byte code_lo8(func)
  .byte code_hi8(func)
  .short code(func)

.data
data:
  .short 0

# CHECK: Relocations [
# CHECK: Section {{.*}} .rela.text {
# CHECK-NEXT: 0x0 R_RISCC_LO8 .data 0x0
# CHECK-NEXT: 0x2 R_RISCC_HI8 .data 0x0
# CHECK-NEXT: 0x4 R_RISCC_HI8 .data 0x0
# CHECK-NEXT: 0x6 R_RISCC_LO8 .data 0x0
# CHECK-NEXT: 0x8 R_RISCC_CODE_HI8 func 0x0
# CHECK-NEXT: 0xA R_RISCC_CODE_LO8 func 0x0
# CHECK-NEXT: 0xE R_RISCC_CODE16 func 0x0
# CHECK-NEXT: 0x10 R_RISCC_PCREL8_WORD external_target 0x0
# CHECK-NEXT: 0x12 R_RISCC_ABS8 .data 0x0
# CHECK-NEXT: 0x13 R_RISCC_ABS16 .data 0x0
# CHECK-NEXT: 0x15 R_RISCC_ABS32 .data 0x0
# CHECK-NEXT: 0x19 R_RISCC_CODE_LO8 func 0x0
# CHECK-NEXT: 0x1A R_RISCC_CODE_HI8 func 0x0
# CHECK-NEXT: 0x1B R_RISCC_CODE16 func 0x0
# CHECK-NEXT: }
# CHECK-NEXT: ]
