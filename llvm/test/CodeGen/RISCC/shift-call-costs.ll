; REQUIRES: riscc-registered-target
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=CALL,RC16
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=SIZE16,RC16
; RUN: opt -mtriple=riscc -mcpu=min -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=SIZE16MIN,RC16
; RUN: opt -mtriple=riscc -mcpu=nano -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=SIZE16MIN,RC16
; RUN: opt -mtriple=riscc -mcpu=nano -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=CALL,RC16
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 \
; RUN:   -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency -disable-output - 2>&1 | \
; RUN:   FileCheck %s --check-prefixes=CALL,RC32
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 \
; RUN:   -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size -disable-output - 2>&1 | FileCheck %s \
; RUN:   --check-prefix=SIZE32
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -riscc-shift-call-cost=false -passes='print<cost-model>' \
; RUN:   -cost-kind=size-latency -disable-output %s 2>&1 | FileCheck %s --check-prefix=OFF

target datalayout = "e-p:16:16"

; Unknown and moderately bounded native shifts become calls in speed builds.
; Short 0..3 shifts keep their existing inline-loop costing, as do constants.
define i16 @native_shifts(i16 %x, i16 %count) {
; CALL-LABEL: function 'native_shifts'
; CALL: cost of 10 for instruction: %a = shl
; CALL: cost of 10 for instruction: %b = lshr
; CALL: cost of 10 for instruction: %c = ashr
; CALL: cost of 1 for instruction: %d = shl
; CALL: cost of 10 for instruction: %e = shl
; CALL: cost of 1 for instruction: %f = shl
; SIZE16-LABEL: function 'native_shifts'
; SIZE16: cost of 2 for instruction: %a = shl
; SIZE16: cost of 2 for instruction: %b = lshr
; SIZE16: cost of 2 for instruction: %c = ashr
; SIZE16: cost of 1 for instruction: %d = shl
; SIZE16: cost of 2 for instruction: %e = shl
; SIZE16: cost of 1 for instruction: %f = shl
; SIZE16MIN-LABEL: function 'native_shifts'
; SIZE16MIN: cost of 3 for instruction: %a = shl
; SIZE16MIN: cost of 3 for instruction: %b = lshr
; SIZE16MIN: cost of 3 for instruction: %c = ashr
; SIZE16MIN: cost of 1 for instruction: %d = shl
; SIZE16MIN: cost of 3 for instruction: %e = shl
; SIZE16MIN: cost of 1 for instruction: %f = shl
; SIZE32-LABEL: function 'native_shifts'
; SIZE32: cost of 4 for instruction: %a = shl
; SIZE32: cost of 4 for instruction: %b = lshr
; SIZE32: cost of 4 for instruction: %c = ashr
; SIZE32: cost of 1 for instruction: %d = shl
; SIZE32: cost of 4 for instruction: %e = shl
; SIZE32: cost of 1 for instruction: %f = shl
; OFF-LABEL: function 'native_shifts'
; OFF: cost of 1 for instruction: %a = shl
  %a = shl i16 %x, %count
  %b = lshr i16 %x, %count
  %c = ashr i16 %x, %count
  %small = and i16 %count, 3
  %d = shl i16 %x, %small
  %medium = and i16 %count, 7
  %e = shl i16 %x, %medium
  %f = shl i16 %x, 1
  ret i16 %a
}

; RC16 size builds use an inline loop; RC32 size builds use a compact helper.
define i16 @size_shift(i16 %x, i16 %count) optsize {
; RC16-LABEL: function 'size_shift'
; RC16: cost of 1 for instruction: %shift = lshr
; RC32-LABEL: function 'size_shift'
; RC32: cost of 10 for instruction: %shift = lshr
; SIZE32-LABEL: function 'size_shift'
; SIZE32: cost of 4 for instruction: %shift = lshr
  %shift = lshr i16 %x, %count
  ret i16 %shift
}

; A guarded helper plus arithmetic should not become unconditional work.
; SimplifyCFG deliberately permits a single expensive instruction, so include
; a dependent add to exercise its aggregate speculation budget.
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes=simplifycfg -S %s | FileCheck %s --check-prefix=CFG
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -riscc-shift-call-cost=false -passes=simplifycfg -S %s | \
; RUN:   FileCheck %s --check-prefix=CFG-OFF
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes='default<O2>' -S %s | llc -mtriple=riscc -mcpu=full \
; RUN:   -verify-machineinstrs | FileCheck %s --check-prefix=ASM
define i16 @guarded(i16 %x, i16 %count, i1 %take) {
; CFG-LABEL: define i16 @guarded
; CFG: br i1 %take,
; CFG: shift:
; CFG-NEXT: %shifted = lshr
; CFG-NOT: select
; CFG-OFF-LABEL: define i16 @guarded
; CFG-OFF-NOT: br i1
; CFG-OFF: select i1 %take
; ASM-LABEL: guarded:
; ASM-NOT: jall
; ASM: bnez [[SHIFT:.LBB[0-9_]+]]
; ASM: ldi r1, 0
; ASM-NEXT: ret
; ASM: [[SHIFT]]:
; ASM: jall s7, __riscc_lshrhi_fast
entry:
  br i1 %take, label %shift, label %done
shift:
  %shifted = lshr i16 %x, %count
  %adjusted = add i16 %shifted, 1
  br label %done
done:
  %result = phi i16 [ 0, %entry ], [ %adjusted, %shift ]
  ret i16 %result
}
