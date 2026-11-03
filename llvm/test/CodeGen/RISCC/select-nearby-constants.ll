; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=full -mattr=+rc32 < %s | FileCheck %s
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=full | FileCheck %s
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=min | FileCheck %s
; RUN: sed 's/i32/i16/g' %s | llc -mtriple=riscc -mcpu=nano | FileCheck %s --check-prefix=NANO

; A boolean plus a byte immediate is shorter than choosing two constants.
; CHECK-LABEL: adjacent:
; CHECK:       slt
; CHECK-NOT:   beqz
; CHECK-NOT:   bnez
; CHECK:       addi {{r[0-7]}}, 2
; CHECK:       ret
 define i32 @adjacent(i32 %a, i32 %b) {
   %cond = icmp slt i32 %a, %b
   %result = select i1 %cond, i32 3, i32 2
   ret i32 %result
 }

; A distance of two needs one doubling, including on profiles without wide
; shifts. The unsigned comparison is native on Nano too.
; CHECK-LABEL: two_apart:
; CHECK:       sltu
; CHECK-NOT:   beqz
; CHECK-NOT:   bnez
; CHECK:       add
; CHECK:       addi {{r[0-7]}}, 2
; CHECK:       ret
; NANO-LABEL: two_apart:
; NANO:       sltu
; NANO-NOT:   beqz
; NANO-NOT:   bnez
; NANO:       add
; NANO:       addi {{r[0-7]}}, 2
 define i32 @two_apart(i32 %a, i32 %b) {
   %cond = icmp ult i32 %a, %b
   %result = select i1 %cond, i32 4, i32 2
   ret i32 %result
 }

; Reversing the order also reverses the condition, including equality.
; CHECK-LABEL: descending:
; CHECK:       sltu
; CHECK:       xori {{r[0-7]}}, 1
; CHECK:       addi {{r[0-7]}}, 2
; CHECK-NOT:   bnez
; CHECK:       ret
 define i32 @descending(i32 %a, i32 %b) {
   %cond = icmp ult i32 %a, %b
   %result = select i1 %cond, i32 2, i32 3
   ret i32 %result
 }

; Forming an equality boolean needs more work than branching on a comparison.
; Keep this small diamond instead of replacing its four instructions by five.
; CHECK-LABEL: equality_byte:
; CHECK:       cmpi {{r[0-7]}}, 120
; CHECK:       {{beqz|bnez}}
; CHECK:       ret
 define i32 @equality_byte(i32 %a) optsize {
   %cond = icmp eq i32 %a, 120
   %result = select i1 %cond, i32 4, i32 2
   ret i32 %result
 }

; Signed immediate bounds are cheap branches, but their boolean comparison
; needs a separately materialized bound. Negative output constants do not
; justify that extra cost.
; CHECK-LABEL: equality_negative:
; CHECK:       cmpi {{r[0-7]}}, -5
; CHECK:       {{beqz|bnez}}
; CHECK:       ret
 define i32 @equality_negative(i32 %a) optsize {
   %cond = icmp eq i32 %a, -5
   %result = select i1 %cond, i32 -1, i32 -2
   ret i32 %result
 }

; A two-bit shift is still cheaper than a select, even on iterative cores.
; CHECK-LABEL: four_or_zero:
; CHECK:       sltu
; CHECK-NOT:   beqz
; CHECK-NOT:   bnez
; CHECK:       {{slli|add}}
; CHECK:       ret
; NANO-LABEL: four_or_zero:
; NANO:       sltu
; NANO-NOT:   beqz
; NANO-NOT:   bnez
; NANO:       add
; NANO:       add
 define i32 @four_or_zero(i32 %a, i32 %b) optsize {
   %cond = icmp ult i32 %a, %b
   %result = select i1 %cond, i32 4, i32 0
   ret i32 %result
 }

; Account for shift latency, not just its instruction count. Shifting five
; positions is slower than this branch, including on Full with wide shifts.
; CHECK-LABEL: large_gap:
; CHECK:       {{beqz|bnez}}
; CHECK:       ret
 define i32 @large_gap(i32 %a, i32 %b) {
   %cond = icmp ult i32 %a, %b
   %result = select i1 %cond, i32 32, i32 0
   ret i32 %result
 }
