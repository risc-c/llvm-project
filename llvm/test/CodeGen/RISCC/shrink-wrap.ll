; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -riscc-shrink-wrap -verify-machineinstrs < %s | FileCheck %s \
; RUN:   --check-prefixes=RC16
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 \
; RUN:   -riscc-shrink-wrap -verify-machineinstrs -o - | FileCheck %s --check-prefixes=RC32
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -O2 -riscc-shrink-wrap -verify-machineinstrs < %s | FileCheck %s \
; RUN:   --check-prefix=NANO
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -riscc-shrink-wrap=false -verify-machineinstrs < %s | FileCheck %s \
; RUN:   --check-prefix=DISABLED

target datalayout = "e-p:16:16"
target triple = "riscc-none-elf"

declare i16 @callee(i16)

; The hot return avoids all frame setup. The cold path is a non-tail call,
; so it saves and restores the incoming public link around the call.
define i16 @hot_cold(i16 %x, i1 %take) nounwind {
; RC16-LABEL: hot_cold:
; RC16: andi r0, 1
; RC16: beqz [[PUBLIC_COLD:.LBB[0-9_]+]]
; RC16-NOT: addi r7,
; RC16-NOT: mfs
; RC16-NOT: st r
; RC16: ret s7
; RC16: [[PUBLIC_COLD]]:
; RC16: addi r7, -2
; RC16: mfs r0, s7
; RC16: st r0, [r7 + 0]
; RC16: jall s7, callee
; RC16: ld r0, [r7 + 0]
; RC16: addi r1, 1
; RC16: addi r7, 2
; RC16: jalr s0, r0
; RC32-LABEL: hot_cold:
; RC32: andi r0, 1
; RC32: beqz [[PUBLIC_COLD32:.LBB[0-9_]+]]
; RC32-NOT: addi r7,
; RC32-NOT: mfs
; RC32-NOT: st r
; RC32: ret s7
; RC32: [[PUBLIC_COLD32]]:
; RC32: addi r7, -4
; RC32: mfs r0, s7
; RC32: st r0, [r7 + 0]
; RC32: ldpc r0,
; RC32: jalr s7, r0
; RC32: ld r0, [r7 + 0]
; RC32: addi r1, 1
; RC32: addi r7, 4
; RC32: jalr s0, r0
; NANO-LABEL: hot_cold:
; NANO: addi r7, -2
; NANO: beqz [[NANO_COLD:.LBB[0-9_]+]]
; NANO: addi r7, 2
; NANO: jalr r0, r6
; NANO: [[NANO_COLD]]:
; NANO: st r6, [r7 + 0]
; NANO: ldi16 r0, callee
; NANO: jalr r6, r0
; DISABLED-LABEL: hot_cold:
; DISABLED: addi r7, -2
; DISABLED: mfs r0, s7
; DISABLED: st r0, [r7 + 0]
; DISABLED: beqz
  br i1 %take, label %fast, label %cold, !prof !0
fast:
  ret i16 %x
cold:
  %called = call i16 @callee(i16 %x)
  %adjusted = add i16 %called, 1
  ret i16 %adjusted
}

; Private functions use S3 as their incoming link. A call to the external
; public callee uses S7, so the private entry link must be preserved on cold.
define internal i16 @private_hot_cold(i16 %x, i1 %take) nounwind noinline {
; RC16-LABEL: private_hot_cold:
; RC16: andi r0, 1
; RC16: beqz [[PRIVATE_COLD:.LBB[0-9_]+]]
; RC16-NOT: addi r7,
; RC16-NOT: mfs
; RC16-NOT: st r
; RC16: ret s3
; RC16: [[PRIVATE_COLD]]:
; RC16: addi r7, -2
; RC16: mfs r0, s3
; RC16: st r0, [r7 + 0]
; RC16: jall s7, callee
; RC16: ld r0, [r7 + 0]
; RC16: addi r1, 1
; RC16: addi r7, 2
; RC16: jalr s0, r0
; RC32-LABEL: private_hot_cold:
; RC32: andi r0, 1
; RC32: beqz [[PRIVATE_COLD32:.LBB[0-9_]+]]
; RC32-NOT: addi r7,
; RC32-NOT: mfs
; RC32-NOT: st r
; RC32: ret s3
; RC32: [[PRIVATE_COLD32]]:
; RC32: addi r7, -4
; RC32: mfs r0, s3
; RC32: st r0, [r7 + 0]
; RC32: ldpc r0,
; RC32: jalr s7, r0
; RC32: ld r0, [r7 + 0]
; RC32: addi r1, 1
; RC32: addi r7, 4
; RC32: jalr s0, r0
  br i1 %take, label %fast, label %cold, !prof !0
fast:
  ret i16 %x
cold:
  %called = call i16 @callee(i16 %x)
  %adjusted = add i16 %called, 1
  ret i16 %adjusted
}

define i16 @enter_private(i16 %x, i1 %take) nounwind noinline {
; RC16-LABEL: enter_private:
; RC16: jall s3, private_hot_cold
; RC32-LABEL: enter_private:
; RC32: ldpc r0,
; RC32: jalr s3, r0
  %r = call i16 @private_hot_cold(i16 %x, i1 %take)
  ret i16 %r
}

!0 = !{!"branch_weights", i32 1000, i32 1}
