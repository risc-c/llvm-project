# REQUIRES: riscc-registered-target
# RUN: llvm-mc -triple=riscc-none-elf -mcpu=full -filetype=obj < %s | llvm-readobj -r - | FileCheck %s

.text
  li r1, tpoff(tls_initialized)
  ldi r2, tpoff(tls_zeroed)
  lui r3, tpoff(tls_zeroed)

.section .tdata,"awT",@progbits
tls_initialized:
  .short 0x1234

.section .tbss,"awT",@nobits
tls_zeroed:
  .space 2

# CHECK: Relocations [
# CHECK: Section {{.*}} .rela.text {
# CHECK-NEXT: 0x0 R_RISCC_TPOFF_HI8 tls_initialized 0x0
# CHECK-NEXT: 0x2 R_RISCC_TPOFF_LO8 tls_initialized 0x0
# CHECK-NEXT: 0x4 R_RISCC_TPOFF_LO8 tls_zeroed 0x0
# CHECK-NEXT: 0x6 R_RISCC_TPOFF_HI8 tls_zeroed 0x0
# CHECK-NEXT: }
# CHECK-NEXT: ]
