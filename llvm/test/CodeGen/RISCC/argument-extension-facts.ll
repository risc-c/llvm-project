; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -verify-machineinstrs %s -o - | FileCheck %s --check-prefixes=ON,CHECK
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -verify-machineinstrs %s -o - | FileCheck %s --check-prefixes=ON,CHECK
; RUN: llc -enable-new-pm -mtriple=riscc -mcpu=full -mattr=+rc32 -riscc-argument-widths -verify-machineinstrs %s -o - \
; RUN:   | FileCheck %s --check-prefixes=ON,CHECK
; RUN: llc -mtriple=riscc -mcpu=min -riscc-argument-widths -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=ON,CHECK
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -riscc-argument-widths -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=ON,CHECK
; RUN: llc -mtriple=riscc -mcpu=sys -riscc-argument-widths -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=ON,CHECK
; RUN: llc -mtriple=riscc -mcpu=sys -mattr=+rc32 -riscc-argument-widths -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=ON,CHECK
; RUN: llc -mtriple=riscc -mcpu=nano -riscc-argument-widths -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=ON,CHECK
; RUN: llc -mtriple=riscc -mcpu=full -riscc-argument-widths=false -verify-machineinstrs %s -o - | FileCheck %s \
; RUN:   --check-prefixes=OFF,CHECK
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 -riscc-argument-widths=false -verify-machineinstrs %s -o - | \
; RUN:   FileCheck %s --check-prefixes=OFF,CHECK

; The ABI normalizes the entry value. Right shifts and bounded XORs preserve
; that property around the loop; the machine SSA analysis must retain it.
define signext i8 @signed_input(i8 signext %start, i16 %count) optsize {
; ON-LABEL: signed_input:
; ON-NOT: andi
; ON: srai
; ON-NOT: andi
; ON: {{ret s7|jalr r0, r6}}
; OFF-LABEL: signed_input:
; OFF: andi {{r[0-6]}}, 255
; OFF: srai
entry:
  br label %loop
loop:
  %i = phi i16 [ %count, %entry ], [ %remaining, %loop ]
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %shift = ashr i8 %value, 1
  %next = xor i8 %shift, 64
  %remaining = add i16 %i, -1
  %done = icmp eq i16 %remaining, 0
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; A narrowing add can wrap: an entry extension does not survive this update.
define signext i8 @wrapping_input(i8 signext %start, i16 %count) optsize {
; CHECK-LABEL: wrapping_input:
; CHECK: andi {{r[0-6]}}, 255
entry:
  br label %loop
loop:
  %i = phi i16 [ %count, %entry ], [ %remaining, %loop ]
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %next = add i8 %value, 79
  %remaining = add i16 %i, -1
  %done = icmp eq i16 %remaining, 0
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

define zeroext i8 @extended_input(i8 zeroext %start, i16 %count) optsize {
; ON-LABEL: extended_input:
; ON-NOT: andi
; ON: {{ret s7|jalr r0, r6}}
; OFF-LABEL: extended_input:
; OFF: andi {{r[0-6]}}, 255
entry:
  br label %loop
loop:
  %i = phi i16 [ %count, %entry ], [ %remaining, %loop ]
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %shift = lshr i8 %value, 1
  %next = xor i8 %shift, 156
  %remaining = add i16 %i, -1
  %done = icmp eq i16 %remaining, 0
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; Without zeroext, the incoming register's upper bits are unspecified. The
; initial mask is necessary even though the IR argument itself has type i8.
define zeroext i8 @unspecified_input(i8 %start, i16 %count) optsize {
; CHECK-LABEL: unspecified_input:
; CHECK: andi {{r[0-6]}}, 255
entry:
  br label %loop
loop:
  %i = phi i16 [ %count, %entry ], [ %remaining, %loop ]
  %value = phi i8 [ %start, %entry ], [ %next, %loop ]
  %shift = lshr i8 %value, 1
  %next = xor i8 %shift, 156
  %remaining = add i16 %i, -1
  %done = icmp eq i16 %remaining, 0
  br i1 %done, label %exit, label %loop
exit:
  ret i8 %next
}

; A wide (possibly split) argument has no narrow extension contract.
define zeroext i8 @wide_input(i32 %start, i16 %count) optsize {
; CHECK-LABEL: wide_input:
; CHECK: andi {{r[0-6]}}, 255
entry:
  br label %loop
loop:
  %i = phi i16 [ %count, %entry ], [ %remaining, %loop ]
  %value = phi i32 [ %start, %entry ], [ %next, %loop ]
  %shift = lshr i32 %value, 1
  %next = xor i32 %shift, 156
  %remaining = add i16 %i, -1
  %done = icmp eq i16 %remaining, 0
  br i1 %done, label %exit, label %loop
exit:
  %result = trunc i32 %next to i8
  ret i8 %result
}
