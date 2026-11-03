; REQUIRES: riscc-registered-target
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size -disable-output %s 2>&1 | FileCheck %s --check-prefixes=SIZE16,CONST16
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=throughput -disable-output %s 2>&1 | FileCheck %s --check-prefixes=CALL,CONST16
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size -disable-output - 2>&1 | FileCheck %s --check-prefixes=SIZE32,CONST32
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=throughput -disable-output - 2>&1 | FileCheck %s --check-prefixes=CALL,CONST32
; RUN: opt -mtriple=riscc -mcpu=min -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size -disable-output %s 2>&1 | FileCheck %s --check-prefixes=SIZEMIN,CONST16
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes=simplifycfg -S %s | FileCheck %s --check-prefix=CFG

target datalayout = "e-p:16:16"

declare i16 @llvm.fshl.i16(i16, i16, i16)
declare i16 @llvm.fshr.i16(i16, i16, i16)

; The constant reaches the target's funnel costing: a 13-bit RC32 rotate
; requires six immediate shifts/OR instructions, while RC16 uses three
; right-funnel steps. Small two-input funnels have their own sequences.
define i16 @constants(i16 %x, i16 %y) {
; CONST16-LABEL: function 'constants'
; CONST16: cost of 3 for instruction: %rotate = call
; CONST32-LABEL: function 'constants'
; CONST32: cost of 6 for instruction: %rotate = call
; CONST16: cost of 1 for instruction: %one = call
; CONST32: cost of 1 for instruction: %one = call
; CONST16: cost of 3 for instruction: %two = call
; CONST32: cost of 3 for instruction: %two = call
; CONST16: cost of 0 for instruction: %zero = call
; CONST32: cost of 0 for instruction: %zero = call
  %rotate = call i16 @llvm.fshl.i16(i16 %x, i16 %x, i16 13)
  %one = call i16 @llvm.fshl.i16(i16 %x, i16 %y, i16 1)
  %two = call i16 @llvm.fshr.i16(i16 %x, i16 %y, i16 2)
  %zero = call i16 @llvm.fshl.i16(i16 %x, i16 %y, i16 0)
  ret i16 %rotate
}

; Count preparation is separate from the shared helper body. A right rotate
; negates then masks even when the original count was already in range.
define i16 @variables(i16 %x, i16 %n) optsize {
; SIZE16-LABEL: function 'variables'
; SIZE16: cost of 3 for instruction: %left = call
; SIZE16: cost of 2 for instruction: %bounded = call
; SIZE16: cost of 5 for instruction: %right = call
; SIZE32-LABEL: function 'variables'
; SIZE32: cost of 5 for instruction: %left = call
; SIZE32: cost of 4 for instruction: %bounded = call
; SIZE32: cost of 7 for instruction: %right = call
; SIZEMIN-LABEL: function 'variables'
; SIZEMIN: cost of 4 for instruction: %left = call
; SIZEMIN: cost of 3 for instruction: %bounded = call
; SIZEMIN: cost of 6 for instruction: %right = call
; CALL-LABEL: function 'variables'
; CALL: cost of 11 for instruction: %left = call
; CALL: cost of 10 for instruction: %bounded = call
; CALL: cost of 13 for instruction: %right = call
  %masked = and i16 %n, 7
  %left = call i16 @llvm.fshl.i16(i16 %x, i16 %x, i16 %n)
  %bounded = call i16 @llvm.fshl.i16(i16 %x, i16 %x, i16 %masked)
  %right = call i16 @llvm.fshr.i16(i16 %x, i16 %x, i16 %masked)
  ret i16 %left
}

; Avoid speculating a helper and its dependent arithmetic across a branch.
define i16 @guarded(i16 %x, i16 %n, i1 %take) {
; CFG-LABEL: define i16 @guarded
; CFG: br i1 %take,
; CFG: rotate:
; CFG-NEXT: %shifted = call
; CFG-NOT: select
entry:
  br i1 %take, label %rotate, label %done
rotate:
  %shifted = call i16 @llvm.fshl.i16(i16 %x, i16 %x, i16 %n)
  %adjusted = add i16 %shifted, 1
  br label %done
done:
  %result = phi i16 [0, %entry], [%adjusted, %rotate]
  ret i16 %result
}
