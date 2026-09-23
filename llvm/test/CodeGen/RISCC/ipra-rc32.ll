; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -enable-new-pm -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs -print-after=reg-usage-propagation -o /dev/null < %s 2>&1 | FileCheck %s --check-prefix=IPRA

target triple = "riscc-none-elf"

declare i32 @unknown(i32)

define internal i32 @leaf(i32 %value) noinline {
  %result = add i32 %value, 1
  ret i32 %result
}

; The direct target must remain a function operand through IPRA. The callee
; preserves R2, so keep the second argument there across the call. R0 and S0
; remain clobbered because late call and branch expansion can use them.
define i32 @known(i32 %value, i32 %keep) {
; IPRA-LABEL: # Machine code for function known:
; IPRA: CALL32_LITERAL @leaf, <regmask $r2 $r3 $r4 $r5 $r6 $r7 $s1 $s2 $s3 $s4 $s5 $s6 $s7>
; ASM-LABEL: known:
; ASM-NOT:   addi r7,
; ASM:       mfs r0, s7
; ASM-NEXT:  mts s2, r0
; ASM-NOT:   st
; ASM:       jalr s7, r0
; ASM:       add r1, r1, r2
; ASM:       ret s2
; ASM:       .long call_target(leaf)
  %result = call i32 @leaf(i32 %value)
  %sum = add i32 %result, %keep
  ret i32 %sum
}

; A declaration without a body still uses the ABI clobbers. Preserve the
; live argument in a callee-saved register, saving and restoring that register.
define i32 @opaque(i32 %value, i32 %keep) {
; IPRA-LABEL: # Machine code for function opaque:
; IPRA: CALL32_LITERAL @unknown, <regmask $r4 $r5 $r6 $r7 $s5 $s6>
; ASM-LABEL: opaque:
; ASM:       st [[SAVED:r[456]]],
; ASM:       mov [[SAVED]], r2
; ASM:       jalr s7, r0
; ASM:       add r1, r1, [[SAVED]]
; ASM:       ld [[SAVED]],
; ASM:       ret s7
  %result = call i32 @unknown(i32 %value)
  %sum = add i32 %result, %keep
  ret i32 %sum
}

; Tail calls also retain their direct target until final literal placement.
define i32 @tail(i32 %value) {
; IPRA-LABEL: # Machine code for function tail:
; IPRA: TAIL32_LITERAL @leaf, <regmask $r2 $r3 $r4 $r5 $r6 $r7 $s1 $s2 $s3 $s4 $s5 $s6 $s7>
; ASM-LABEL: tail:
; ASM:       ldpc r0,
; ASM-NEXT:  jalr s0, r0
; ASM-NOT:   ret
; ASM:       .long call_target(leaf)
  %result = tail call i32 @leaf(i32 %value)
  ret i32 %result
}

; Repeated calls in a function with no return need no link save, regardless
; of whether IPRA can inspect the callee.
define void @call_forever() noreturn {
; ASM-LABEL: call_forever:
; ASM-NOT:   addi r7,
; ASM-NOT:   mfs
; ASM-NOT:   mts
; ASM-NOT:   st
; ASM:       jalr s7, r0
entry:
  br label %loop
loop:
  %unused = call i32 @unknown(i32 1)
  br label %loop
}
