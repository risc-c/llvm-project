; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=RC16,RC16OS
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=RC16,RC16OS
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=RC32,RC32OS
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -O2 -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=RC32,RC32OS

target triple = "riscc-none-elf"

; Counts masked to three or one are known to fit the native word.  All three
; directions must remain inline, including RC32 where a general variable shift
; otherwise uses a helper.
define i16 @shl16_b3(i16 %value, i16 %count) {
; RC16-LABEL: shl16_b3:
; RC16:       andi [[COUNT:r[0-6]]], 3
; RC16-NOT:   __riscc_shlhi_fast
; RC16:       beqz
; RC16:       {{slli|add}} r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC16:       bnez
; RC16-NOT:   jal
; RC16:       ret
; RC16-NOT:   __
; RC16:       .size shl16_b3,
  %bounded = and i16 %count, 3
  %result = shl i16 %value, %bounded
  ret i16 %result
}

define i16 @lshr16_b3(i16 %value, i16 %count) {
; RC16-LABEL: lshr16_b3:
; RC16:       andi [[COUNT:r[0-6]]], 3
; RC16-NOT:   __riscc_lshrhi_fast
; RC16:       beqz
; RC16:       srli r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC16:       bnez
; RC16-NOT:   jal
; RC16:       ret
; RC16-NOT:   __
; RC16:       .size lshr16_b3,
  %bounded = and i16 %count, 3
  %result = lshr i16 %value, %bounded
  ret i16 %result
}

define i16 @ashr16_b3(i16 %value, i16 %count) {
; RC16-LABEL: ashr16_b3:
; RC16:       andi [[COUNT:r[0-6]]], 3
; RC16-NOT:   __riscc_ashrhi_fast
; RC16:       beqz
; RC16:       srai r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC16:       bnez
; RC16-NOT:   jal
; RC16:       ret
; RC16-NOT:   __
; RC16:       .size ashr16_b3,
  %bounded = and i16 %count, 3
  %result = ashr i16 %value, %bounded
  ret i16 %result
}

define i16 @shl16_b1(i16 %value, i16 %count) {
; RC16-LABEL: shl16_b1:
; RC16:       andi [[COUNT:r[0-6]]], 1
; RC16-NOT:   __riscc_shlhi_fast
; RC16:       beqz
; RC16:       {{slli|add}} r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC16:       bnez
; RC16-NOT:   jal
; RC16:       ret
; RC16-NOT:   __
; RC16:       .size shl16_b1,
  %bounded = and i16 %count, 1
  %result = shl i16 %value, %bounded
  ret i16 %result
}

define i16 @lshr16_b1(i16 %value, i16 %count) {
; RC16-LABEL: lshr16_b1:
; RC16:       andi [[COUNT:r[0-6]]], 1
; RC16-NOT:   __riscc_lshrhi_fast
; RC16:       beqz
; RC16:       srli r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC16:       bnez
; RC16-NOT:   jal
; RC16:       ret
; RC16-NOT:   __
; RC16:       .size lshr16_b1,
  %bounded = and i16 %count, 1
  %result = lshr i16 %value, %bounded
  ret i16 %result
}

define i16 @ashr16_b1(i16 %value, i16 %count) {
; RC16-LABEL: ashr16_b1:
; RC16:       andi [[COUNT:r[0-6]]], 1
; RC16-NOT:   __riscc_ashrhi_fast
; RC16:       beqz
; RC16:       srai r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC16:       bnez
; RC16-NOT:   jal
; RC16:       ret
; RC16-NOT:   __
; RC16:       .size ashr16_b1,
  %bounded = and i16 %count, 1
  %result = ashr i16 %value, %bounded
  ret i16 %result
}

; Larger and unbounded RC16 counts retain the existing fast helper policy.
define i16 @shl16_b7(i16 %value, i16 %count) {
; RC16-LABEL: shl16_b7:
; RC16:       __riscc_shlhi_fast
  %bounded = and i16 %count, 7
  %result = shl i16 %value, %bounded
  ret i16 %result
}

define i16 @lshr16_b7(i16 %value, i16 %count) {
; RC16-LABEL: lshr16_b7:
; RC16:       __riscc_lshrhi_fast
  %bounded = and i16 %count, 7
  %result = lshr i16 %value, %bounded
  ret i16 %result
}

define i16 @ashr16_b7(i16 %value, i16 %count) {
; RC16-LABEL: ashr16_b7:
; RC16:       __riscc_ashrhi_fast
  %bounded = and i16 %count, 7
  %result = ashr i16 %value, %bounded
  ret i16 %result
}

define i16 @shl16_var(i16 %value, i16 %count) {
; RC16-LABEL: shl16_var:
; RC16:       __riscc_shlhi_fast
  %result = shl i16 %value, %count
  ret i16 %result
}

define i16 @lshr16_var(i16 %value, i16 %count) {
; RC16-LABEL: lshr16_var:
; RC16:       __riscc_lshrhi_fast
  %result = lshr i16 %value, %count
  ret i16 %result
}

define i16 @ashr16_var(i16 %value, i16 %count) {
; RC16-LABEL: ashr16_var:
; RC16:       __riscc_ashrhi_fast
  %result = ashr i16 %value, %count
  ret i16 %result
}

; Native RC32 uses the same bounded inline path, while larger counts keep the
; helper and mask the amount to the 32-bit word.
define i32 @shl32_b3(i32 %value, i32 %count) {
; RC32-LABEL: shl32_b3:
; RC32:       andi [[COUNT:r[0-6]]], 3
; RC32-NOT:   __riscc_shlsi_fast
; RC32:       beqz
; RC32:       {{slli|add}} r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC32:       bnez
; RC32-NOT:   jal
; RC32:       ret
; RC32-NOT:   __
; RC32:       .size shl32_b3,
  %bounded = and i32 %count, 3
  %result = shl i32 %value, %bounded
  ret i32 %result
}

define i32 @lshr32_b3(i32 %value, i32 %count) {
; RC32-LABEL: lshr32_b3:
; RC32:       andi [[COUNT:r[0-6]]], 3
; RC32-NOT:   __riscc_lshrsi_fast
; RC32:       beqz
; RC32:       srli r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC32:       bnez
; RC32-NOT:   jal
; RC32:       ret
; RC32-NOT:   __
; RC32:       .size lshr32_b3,
  %bounded = and i32 %count, 3
  %result = lshr i32 %value, %bounded
  ret i32 %result
}

define i32 @ashr32_b3(i32 %value, i32 %count) {
; RC32-LABEL: ashr32_b3:
; RC32:       andi [[COUNT:r[0-6]]], 3
; RC32-NOT:   __riscc_ashrsi_fast
; RC32:       beqz
; RC32:       srai r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC32:       bnez
; RC32-NOT:   jal
; RC32:       ret
; RC32-NOT:   __
; RC32:       .size ashr32_b3,
  %bounded = and i32 %count, 3
  %result = ashr i32 %value, %bounded
  ret i32 %result
}

define i32 @shl32_b1(i32 %value, i32 %count) {
; RC32-LABEL: shl32_b1:
; RC32:       andi [[COUNT:r[0-6]]], 1
; RC32-NOT:   __riscc_shlsi_fast
; RC32:       beqz
; RC32:       {{slli|add}} r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC32:       bnez
; RC32-NOT:   jal
; RC32:       ret
; RC32-NOT:   __
; RC32:       .size shl32_b1,
  %bounded = and i32 %count, 1
  %result = shl i32 %value, %bounded
  ret i32 %result
}

define i32 @lshr32_b1(i32 %value, i32 %count) {
; RC32-LABEL: lshr32_b1:
; RC32:       andi [[COUNT:r[0-6]]], 1
; RC32-NOT:   __riscc_lshrsi_fast
; RC32:       beqz
; RC32:       srli r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC32:       bnez
; RC32-NOT:   jal
; RC32:       ret
; RC32-NOT:   __
; RC32:       .size lshr32_b1,
  %bounded = and i32 %count, 1
  %result = lshr i32 %value, %bounded
  ret i32 %result
}

define i32 @ashr32_b1(i32 %value, i32 %count) {
; RC32-LABEL: ashr32_b1:
; RC32:       andi [[COUNT:r[0-6]]], 1
; RC32-NOT:   __riscc_ashrsi_fast
; RC32:       beqz
; RC32:       srai r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC32:       bnez
; RC32-NOT:   jal
; RC32:       ret
; RC32-NOT:   __
; RC32:       .size ashr32_b1,
  %bounded = and i32 %count, 1
  %result = ashr i32 %value, %bounded
  ret i32 %result
}

define i32 @shl32_b7(i32 %value, i32 %count) {
; RC32-LABEL: shl32_b7:
; RC32:       __riscc_shlsi_fast
  %bounded = and i32 %count, 7
  %result = shl i32 %value, %bounded
  ret i32 %result
}

define i32 @lshr32_b7(i32 %value, i32 %count) {
; RC32-LABEL: lshr32_b7:
; RC32:       __riscc_lshrsi_fast
  %bounded = and i32 %count, 7
  %result = lshr i32 %value, %bounded
  ret i32 %result
}

define i32 @ashr32_b7(i32 %value, i32 %count) {
; RC32-LABEL: ashr32_b7:
; RC32:       __riscc_ashrsi_fast
  %bounded = and i32 %count, 7
  %result = ashr i32 %value, %bounded
  ret i32 %result
}

define i32 @shl32_var(i32 %value, i32 %count) {
; RC32-LABEL: shl32_var:
; RC32:       __riscc_shlsi_fast
  %result = shl i32 %value, %count
  ret i32 %result
}

define i32 @lshr32_var(i32 %value, i32 %count) {
; RC32-LABEL: lshr32_var:
; RC32:       __riscc_lshrsi_fast
  %result = lshr i32 %value, %count
  ret i32 %result
}

define i32 @ashr32_var(i32 %value, i32 %count) {
; RC32-LABEL: ashr32_var:
; RC32:       __riscc_ashrsi_fast
  %result = ashr i32 %value, %count
  ret i32 %result
}

; A speed-independent optsize sample confirms that the known-small-count path
; is still inline for each direction and native width.
define i16 @shl16_b3_os(i16 %value, i16 %count) optsize {
; RC16OS-LABEL: shl16_b3_os:
; RC16OS-NOT:   __riscc_shlhi_fast
; RC16OS:       beqz
; RC16OS:       {{slli|add}} r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC16OS:       bnez
; RC16OS-NOT:   jal
; RC16OS:       ret
; RC16OS-NOT:   __
; RC16OS:       .size shl16_b3_os,
  %bounded = and i16 %count, 3
  %result = shl i16 %value, %bounded
  ret i16 %result
}

define i16 @lshr16_b3_os(i16 %value, i16 %count) optsize {
; RC16OS-LABEL: lshr16_b3_os:
; RC16OS-NOT:   __riscc_lshrhi_fast
; RC16OS:       beqz
; RC16OS:       srli r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC16OS:       bnez
; RC16OS-NOT:   jal
; RC16OS:       ret
; RC16OS-NOT:   __
; RC16OS:       .size lshr16_b3_os,
  %bounded = and i16 %count, 3
  %result = lshr i16 %value, %bounded
  ret i16 %result
}

define i16 @ashr16_b3_os(i16 %value, i16 %count) optsize {
; RC16OS-LABEL: ashr16_b3_os:
; RC16OS-NOT:   __riscc_ashrhi_fast
; RC16OS:       beqz
; RC16OS:       srai r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC16OS:       bnez
; RC16OS-NOT:   jal
; RC16OS:       ret
; RC16OS-NOT:   __
; RC16OS:       .size ashr16_b3_os,
  %bounded = and i16 %count, 3
  %result = ashr i16 %value, %bounded
  ret i16 %result
}

define i32 @shl32_b3_os(i32 %value, i32 %count) optsize {
; RC32OS-LABEL: shl32_b3_os:
; RC32OS-NOT:   __riscc_shlsi_fast
; RC32OS:       beqz
; RC32OS:       {{slli|add}} r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC32OS:       bnez
; RC32OS-NOT:   jal
; RC32OS:       ret
; RC32OS-NOT:   __
; RC32OS:       .size shl32_b3_os,
  %bounded = and i32 %count, 3
  %result = shl i32 %value, %bounded
  ret i32 %result
}

define i32 @lshr32_b3_os(i32 %value, i32 %count) optsize {
; RC32OS-LABEL: lshr32_b3_os:
; RC32OS-NOT:   __riscc_lshrsi_fast
; RC32OS:       beqz
; RC32OS:       srli r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC32OS:       bnez
; RC32OS-NOT:   jal
; RC32OS:       ret
; RC32OS-NOT:   __
; RC32OS:       .size lshr32_b3_os,
  %bounded = and i32 %count, 3
  %result = lshr i32 %value, %bounded
  ret i32 %result
}

define i32 @ashr32_b3_os(i32 %value, i32 %count) optsize {
; RC32OS-LABEL: ashr32_b3_os:
; RC32OS-NOT:   __riscc_ashrsi_fast
; RC32OS:       beqz
; RC32OS:       srai r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC32OS:       bnez
; RC32OS-NOT:   jal
; RC32OS:       ret
; RC32OS-NOT:   __
; RC32OS:       .size ashr32_b3_os,
  %bounded = and i32 %count, 3
  %result = ashr i32 %value, %bounded
  ret i32 %result
}

define i16 @shl16_b3_minsize(i16 %value, i16 %count) minsize {
; RC16OS-LABEL: shl16_b3_minsize:
; RC16OS-NOT:   __riscc_shlhi_fast
; RC16OS:       beqz
; RC16OS:       {{slli|add}} r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC16OS:       bnez
; RC16OS-NOT:   jal
; RC16OS:       ret
; RC16OS-NOT:   __
; RC16OS:       .size shl16_b3_minsize,
  %bounded = and i16 %count, 3
  %result = shl i16 %value, %bounded
  ret i16 %result
}

define i32 @shl32_b3_minsize(i32 %value, i32 %count) minsize {
; RC32OS-LABEL: shl32_b3_minsize:
; RC32OS-NOT:   __riscc_shlsi_fast
; RC32OS:       beqz
; RC32OS:       {{slli|add}} r{{[0-6]}}, r{{[0-6]}}, {{1|r[0-6]}}
; RC32OS:       bnez
; RC32OS-NOT:   jal
; RC32OS:       ret
; RC32OS-NOT:   __
; RC32OS:       .size shl32_b3_minsize,
  %bounded = and i32 %count, 3
  %result = shl i32 %value, %bounded
  ret i32 %result
}
