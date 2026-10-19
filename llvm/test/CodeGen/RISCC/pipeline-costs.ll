; REQUIRES: riscc-registered-target
; RUN: opt -mtriple=riscc -mcpu=full -passes='print<cost-model>' -cost-kind=latency -disable-output %s 2>&1 | \
; RUN:   FileCheck %s --check-prefix=LATENCY
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' -e 's/align 2/align 4/g' %s | opt -mtriple=riscc -mcpu=full \
; RUN:   -mattr=+rc32 -passes='print<cost-model>' -cost-kind=latency -disable-output - 2>&1 | FileCheck %s \
; RUN:   --check-prefix=LATENCY
; RUN: opt -mtriple=riscc -mcpu=full -passes='print<cost-model>' -cost-kind=throughput -disable-output %s 2>&1 | \
; RUN:   FileCheck %s --check-prefix=THROUGHPUT
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' -e 's/align 2/align 4/g' %s | opt -mtriple=riscc -mcpu=full \
; RUN:   -mattr=+rc32 -passes='print<cost-model>' -cost-kind=throughput -disable-output - 2>&1 | FileCheck %s \
; RUN:   --check-prefix=THROUGHPUT
; RUN: opt -mtriple=riscc -mcpu=full -passes='print<cost-model>' -cost-kind=code-size -disable-output %s 2>&1 | \
; RUN:   FileCheck %s --check-prefix=SIZE
; RUN: opt -mtriple=riscc -mcpu=full -passes='print<cost-model>' -cost-kind=size-latency -disable-output %s 2>&1 | \
; RUN:   FileCheck %s --check-prefix=MIXED
; RUN: opt -mtriple=riscc -mcpu=full -riscc-pipeline-costs=false -passes='print<cost-model>' -cost-kind=latency \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefix=OFF
; RUN: opt -mtriple=riscc -mcpu=min -passes='print<cost-model>' -cost-kind=latency -disable-output %s 2>&1 | \
; RUN:   FileCheck %s --check-prefix=MIN
; RUN: opt -mtriple=riscc -mcpu=nano -passes='print<cost-model>' -cost-kind=latency -disable-output %s 2>&1 | \
; RUN:   FileCheck %s --check-prefix=NANO

target datalayout = "e-p:16:16"

; Query the same latency/resource definitions as machine scheduling. In-order
; MUL occupies Execute for three clocks, but remains one compact instruction.
; Loads can issue every clock and need one independent instruction before use.
define i16 @native(ptr %p, i16 %a, i16 %b) {
; LATENCY-LABEL: function 'native'
; LATENCY: cost of 2 for instruction: %load = load
; LATENCY: cost of 1 for instruction: %add = add
; LATENCY: cost of 3 for instruction: %mul = mul
; LATENCY: cost of 1 for instruction: store
; THROUGHPUT-LABEL: function 'native'
; THROUGHPUT: cost of 1 for instruction: %load = load
; THROUGHPUT: cost of 1 for instruction: %add = add
; THROUGHPUT: cost of 3 for instruction: %mul = mul
; THROUGHPUT: cost of 1 for instruction: store
; SIZE-LABEL: function 'native'
; SIZE: cost of 1 for instruction: %load = load
; SIZE: cost of 1 for instruction: %add = add
; SIZE: cost of 1 for instruction: %mul = mul
; SIZE: cost of 1 for instruction: store
; MIXED-LABEL: function 'native'
; MIXED: cost of 1 for instruction: %load = load
; MIXED: cost of 3 for instruction: %mul = mul
; OFF-LABEL: function 'native'
; OFF: cost of 4 for instruction: %load = load
; OFF: cost of 1 for instruction: %mul = mul
; MIN-LABEL: function 'native'
; MIN: cost of 2 for instruction: %load = load
; MIN: cost of 10 for instruction: %mul = mul
; NANO-LABEL: function 'native'
; NANO: cost of 4 for instruction: %load = load
; NANO: cost of 10 for instruction: %mul = mul
  %load = load i16, ptr %p, align 2
  %add = add i16 %load, %a
  %mul = mul i16 %add, %b
  store i16 %mul, ptr %p, align 2
  ret i16 %mul
}

; Byte loads also use the native pipeline. Unaligned and wider accesses need
; legalization and must not inherit the cost of a single aligned load.
define i8 @other_loads(ptr %p) {
; LATENCY-LABEL: function 'other_loads'
; LATENCY: cost of 2 for instruction: %byte = load
; LATENCY: cost of 4 for instruction: %unaligned = load
; LATENCY: cost of 4 for instruction: %wide = load
  %byte = load i8, ptr %p, align 1
  %unaligned = load i16, ptr %p, align 1
  %wide = load i64, ptr %p, align 8
  ret i8 %byte
}

; Constants retain folding/strength-reduction costing; software helpers keep
; their separate call estimate rather than the hardware multiplier's cost.
define i16 @constant_and_helper(i16 %a, i16 %b) {
; LATENCY-LABEL: function 'constant_and_helper'
; LATENCY: cost of 1 for instruction: %constant = mul
; LATENCY: cost of 10 for instruction: %divide = udiv
  %constant = mul i16 %a, 8
  %divide = udiv i16 %constant, %b
  ret i16 %divide
}
