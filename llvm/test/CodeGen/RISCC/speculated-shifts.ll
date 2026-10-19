; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RC32

; A speculated, out-of-range shift produces poison that select discards.
; Its software lowering must not jump outside the fixed-count staircase.
define i16 @guarded16(i16 %value, i16 %count) {
; RC16-LABEL: guarded16:
; RC16: andi {{r[0-7]}}, 15
; RC16: __riscc_lshrhi_fast
  %valid = icmp ult i16 %count, 8
  %shift = lshr i16 %value, %count
  %result = select i1 %valid, i16 %shift, i16 0
  ret i16 %result
}

define i32 @guarded32(i32 %value, i32 %count) {
; RC32-LABEL: guarded32:
; RC32: andi {{r[0-7]}}, 31
; RC32: __riscc_lshrsi_fast
  %valid = icmp ult i32 %count, 8
  %shift = lshr i32 %value, %count
  %result = select i1 %valid, i32 %shift, i32 0
  ret i32 %result
}

; Do not introduce a second mask for an already bounded count.
define i16 @bounded16(i16 %value, i16 %count) {
; RC16-LABEL: bounded16:
; RC16: andi {{r[0-7]}}, 7
; RC16-NOT: andi
; RC16: __riscc_lshrhi_fast
  %bounded = and i16 %count, 7
  %result = lshr i16 %value, %bounded
  ret i16 %result
}

define i32 @bounded32(i32 %value, i32 %count) {
; RC32-LABEL: bounded32:
; RC32: andi {{r[0-7]}}, 15
; RC32-NOT: andi
; RC32: __riscc_lshrsi_fast
  %bounded = and i32 %count, 15
  %result = lshr i32 %value, %bounded
  ret i32 %result
}

; The compact loop helper also needs defined, bounded arguments, even though
; it does not use the fast helper's computed jump.
define i32 @guarded_size32(i32 %value, i32 %count) optsize {
; RC32-LABEL: guarded_size32:
; RC32: andi {{r[0-7]}}, 31
; RC32: __ashrsi3
  %valid = icmp ult i32 %count, 8
  %shift = ashr i32 %value, %count
  %result = select i1 %valid, i32 %shift, i32 0
  ret i32 %result
}
