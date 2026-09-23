; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=nano -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=SPEED,SIZE
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=SPEED,SIZE
; RUN: llc -mtriple=riscc-none-elf -mcpu=sys -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=SPEED,SIZE
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=SPEED,SIZE
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=DEBUG --implicit-check-not=_fast

; Normal functions use the computed-entry fast helpers. Size attributed functions
; retain no _fast call in either optsize or minsize mode.
target triple = "riscc-none-elf"

define i16 @left(i16 %value, i16 %count) {
; SPEED-LABEL: left:
; SPEED:       __riscc_shlhi_fast
; DEBUG-LABEL: left:
  %result = shl i16 %value, %count
  ret i16 %result
}

define i16 @logical_right(i16 %value, i16 %count) {
; SPEED-LABEL: logical_right:
; SPEED:       __riscc_lshrhi_fast
  %result = lshr i16 %value, %count
  ret i16 %result
}

define i16 @arithmetic_right(i16 %value, i16 %count) {
; SPEED-LABEL: arithmetic_right:
; SPEED:       __riscc_ashrhi_fast
  %result = ashr i16 %value, %count
  ret i16 %result
}

define i16 @left_optsize(i16 %value, i16 %count) #0 {
; SIZE-LABEL: left_optsize:
; SIZE-NOT:   __riscc_shlhi_fast
  %result = shl i16 %value, %count
  ret i16 %result
}

define i16 @logical_right_optsize(i16 %value, i16 %count) #0 {
; SIZE-LABEL: logical_right_optsize:
; SIZE-NOT:   __riscc_lshrhi_fast
  %result = lshr i16 %value, %count
  ret i16 %result
}

define i16 @arithmetic_right_optsize(i16 %value, i16 %count) #0 {
; SIZE-LABEL: arithmetic_right_optsize:
; SIZE-NOT:   __riscc_ashrhi_fast
  %result = ashr i16 %value, %count
  ret i16 %result
}

define i16 @left_minsize(i16 %value, i16 %count) #1 {
; SIZE-LABEL: left_minsize:
; SIZE-NOT:   __riscc_shlhi_fast
  %result = shl i16 %value, %count
  ret i16 %result
}

define i16 @logical_right_minsize(i16 %value, i16 %count) #1 {
; SIZE-LABEL: logical_right_minsize:
; SIZE-NOT:   __riscc_lshrhi_fast
  %result = lshr i16 %value, %count
  ret i16 %result
}

define i16 @arithmetic_right_minsize(i16 %value, i16 %count) #1 {
; SIZE-LABEL: arithmetic_right_minsize:
; SIZE-NOT:   __riscc_ashrhi_fast
  %result = ashr i16 %value, %count
  ret i16 %result
}

attributes #0 = { optsize }
attributes #1 = { minsize }

define i16 @unoptimized_left(i16 %value, i16 %count) noinline optnone {
; SPEED-LABEL: unoptimized_left:
; SPEED-NOT:   __riscc_shlhi_fast
  %result = shl i16 %value, %count
  ret i16 %result
}
