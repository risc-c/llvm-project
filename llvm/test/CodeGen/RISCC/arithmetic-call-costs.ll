; REQUIRES: riscc-registered-target
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=FULL16
; RUN: opt -mtriple=riscc -mcpu=min -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=MIN16
; RUN: opt -mtriple=riscc -mcpu=nano -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=NANO16
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 \
; RUN:   -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency -disable-output - 2>&1 | \
; RUN:   FileCheck %s --check-prefixes=FULL32
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=min -mattr=+rc32 \
; RUN:   -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency -disable-output - 2>&1 | \
; RUN:   FileCheck %s --check-prefixes=MIN32
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=FULL16SIZE
; RUN: opt -mtriple=riscc -mcpu=min -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=MIN16SIZE
; RUN: opt -mtriple=riscc -mcpu=nano -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size \
; RUN:   -disable-output %s 2>&1 | FileCheck %s --check-prefixes=NANO16SIZE
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32 \
; RUN:   -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size -disable-output - 2>&1 | FileCheck %s \
; RUN:   --check-prefixes=FULL32SIZE
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=min -mattr=+rc32 \
; RUN:   -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=code-size -disable-output - 2>&1 | FileCheck %s \
; RUN:   --check-prefixes=MIN32SIZE
; RUN: opt -mtriple=riscc -mcpu=full -mattr=+mdu -codegen-opt-level=2 -passes='print<cost-model>' \
; RUN:   -cost-kind=size-latency -disable-output %s 2>&1 | FileCheck %s --check-prefixes=MDU16
; RUN: sed -e 's/i16/i32/g' -e 's/p:16:16/p:32:32/g' %s | opt -mtriple=riscc -mcpu=full -mattr=+rc32,+mdu \
; RUN:   -codegen-opt-level=2 -passes='print<cost-model>' -cost-kind=size-latency -disable-output - 2>&1 | \
; RUN:   FileCheck %s --check-prefixes=MDU32
; RUN: opt -mtriple=riscc -mcpu=full -codegen-opt-level=2 -riscc-arithmetic-call-cost=false \
; RUN:   -passes='print<cost-model>' -cost-kind=size-latency -disable-output %s 2>&1 | FileCheck %s \
; RUN:   --check-prefixes=OFF

target datalayout = "e-p:16:16"

; Nonconstant signed division/remainder are helper-priced. Unsigned division
; and remainder are helper-priced only without the native MDU feature. A legal
; native multiply remains one instruction for size, but occupies Execute
; for three clocks in the pipeline model.
define i16 @native_arithmetic(i16 %x, i16 %y) {
; FULL16-LABEL: function 'native_arithmetic'
; FULL16: cost of 10 for instruction: %sdiv = sdiv
; FULL16: cost of 10 for instruction: %udiv = udiv
; FULL16: cost of 10 for instruction: %srem = srem
; FULL16: cost of 10 for instruction: %urem = urem
; FULL16: cost of 3 for instruction: %mul = mul
; MIN16-LABEL: function 'native_arithmetic'
; MIN16: cost of 10 for instruction: %sdiv = sdiv
; MIN16: cost of 10 for instruction: %udiv = udiv
; MIN16: cost of 10 for instruction: %srem = srem
; MIN16: cost of 10 for instruction: %urem = urem
; MIN16: cost of 10 for instruction: %mul = mul
; NANO16-LABEL: function 'native_arithmetic'
; NANO16: cost of 10 for instruction: %sdiv = sdiv
; NANO16: cost of 10 for instruction: %udiv = udiv
; NANO16: cost of 10 for instruction: %srem = srem
; NANO16: cost of 10 for instruction: %urem = urem
; NANO16: cost of 10 for instruction: %mul = mul
; FULL32-LABEL: function 'native_arithmetic'
; FULL32: cost of 10 for instruction: %sdiv = sdiv
; FULL32: cost of 10 for instruction: %udiv = udiv
; FULL32: cost of 10 for instruction: %srem = srem
; FULL32: cost of 10 for instruction: %urem = urem
; FULL32: cost of 3 for instruction: %mul = mul
; MIN32-LABEL: function 'native_arithmetic'
; MIN32: cost of 10 for instruction: %sdiv = sdiv
; MIN32: cost of 10 for instruction: %udiv = udiv
; MIN32: cost of 10 for instruction: %srem = srem
; MIN32: cost of 10 for instruction: %urem = urem
; MIN32: cost of 10 for instruction: %mul = mul
; FULL16SIZE-LABEL: function 'native_arithmetic'
; FULL16SIZE: cost of 2 for instruction: %sdiv = sdiv
; FULL16SIZE: cost of 2 for instruction: %udiv = udiv
; FULL16SIZE: cost of 2 for instruction: %srem = srem
; FULL16SIZE: cost of 2 for instruction: %urem = urem
; FULL16SIZE: cost of 1 for instruction: %mul = mul
; MIN16SIZE-LABEL: function 'native_arithmetic'
; MIN16SIZE: cost of 3 for instruction: %sdiv = sdiv
; MIN16SIZE: cost of 3 for instruction: %udiv = udiv
; MIN16SIZE: cost of 3 for instruction: %srem = srem
; MIN16SIZE: cost of 3 for instruction: %urem = urem
; MIN16SIZE: cost of 3 for instruction: %mul = mul
; NANO16SIZE-LABEL: function 'native_arithmetic'
; NANO16SIZE: cost of 3 for instruction: %sdiv = sdiv
; NANO16SIZE: cost of 3 for instruction: %udiv = udiv
; NANO16SIZE: cost of 3 for instruction: %srem = srem
; NANO16SIZE: cost of 3 for instruction: %urem = urem
; NANO16SIZE: cost of 3 for instruction: %mul = mul
; FULL32SIZE-LABEL: function 'native_arithmetic'
; FULL32SIZE: cost of 4 for instruction: %sdiv = sdiv
; FULL32SIZE: cost of 4 for instruction: %udiv = udiv
; FULL32SIZE: cost of 4 for instruction: %srem = srem
; FULL32SIZE: cost of 4 for instruction: %urem = urem
; FULL32SIZE: cost of 1 for instruction: %mul = mul
; MIN32SIZE-LABEL: function 'native_arithmetic'
; MIN32SIZE: cost of 4 for instruction: %sdiv = sdiv
; MIN32SIZE: cost of 4 for instruction: %udiv = udiv
; MIN32SIZE: cost of 4 for instruction: %srem = srem
; MIN32SIZE: cost of 4 for instruction: %urem = urem
; MIN32SIZE: cost of 4 for instruction: %mul = mul
; MDU16-LABEL: function 'native_arithmetic'
; MDU16: cost of 10 for instruction: %sdiv = sdiv
; MDU16: cost of 4 for instruction: %udiv = udiv
; MDU16: cost of 10 for instruction: %srem = srem
; MDU16: cost of 4 for instruction: %urem = urem
; MDU16: cost of 3 for instruction: %mul = mul
; MDU32-LABEL: function 'native_arithmetic'
; MDU32: cost of 10 for instruction: %sdiv = sdiv
; MDU32: cost of 4 for instruction: %udiv = udiv
; MDU32: cost of 10 for instruction: %srem = srem
; MDU32: cost of 4 for instruction: %urem = urem
; MDU32: cost of 3 for instruction: %mul = mul
; OFF-LABEL: function 'native_arithmetic'
; OFF: cost of 4 for instruction: %sdiv = sdiv
; OFF: cost of 4 for instruction: %udiv = udiv
; OFF: cost of 4 for instruction: %srem = srem
; OFF: cost of 4 for instruction: %urem = urem
; OFF: cost of 3 for instruction: %mul = mul
  %sdiv = sdiv i16 %x, %y
  %udiv = udiv i16 %x, %y
  %srem = srem i16 %x, %y
  %urem = urem i16 %x, %y
  %mul = mul i16 %x, %y
  ret i16 %mul
}

; Constant operands retain the generic arithmetic costs instead of being
; treated as helper calls.
define i16 @constant_arithmetic(i16 %x) {
; FULL16-LABEL: function 'constant_arithmetic'
; FULL16: cost of 4 for instruction: %sdiv = sdiv
; FULL16: cost of 4 for instruction: %udiv = udiv
; FULL16: cost of 4 for instruction: %srem = srem
; FULL16: cost of 4 for instruction: %urem = urem
; FULL16: cost of 1 for instruction: %mul = mul
; MIN16-LABEL: function 'constant_arithmetic'
; MIN16: cost of 4 for instruction: %sdiv = sdiv
; MIN16: cost of 4 for instruction: %udiv = udiv
; MIN16: cost of 4 for instruction: %srem = srem
; MIN16: cost of 4 for instruction: %urem = urem
; MIN16: cost of 1 for instruction: %mul = mul
; NANO16-LABEL: function 'constant_arithmetic'
; NANO16: cost of 4 for instruction: %sdiv = sdiv
; NANO16: cost of 4 for instruction: %udiv = udiv
; NANO16: cost of 4 for instruction: %srem = srem
; NANO16: cost of 4 for instruction: %urem = urem
; NANO16: cost of 1 for instruction: %mul = mul
; FULL32-LABEL: function 'constant_arithmetic'
; FULL32: cost of 4 for instruction: %sdiv = sdiv
; FULL32: cost of 4 for instruction: %udiv = udiv
; FULL32: cost of 4 for instruction: %srem = srem
; FULL32: cost of 4 for instruction: %urem = urem
; FULL32: cost of 1 for instruction: %mul = mul
; MIN32-LABEL: function 'constant_arithmetic'
; MIN32: cost of 4 for instruction: %sdiv = sdiv
; MIN32: cost of 4 for instruction: %udiv = udiv
; MIN32: cost of 4 for instruction: %srem = srem
; MIN32: cost of 4 for instruction: %urem = urem
; MIN32: cost of 1 for instruction: %mul = mul
; FULL16SIZE-LABEL: function 'constant_arithmetic'
; FULL16SIZE: cost of 4 for instruction: %sdiv = sdiv
; FULL16SIZE: cost of 4 for instruction: %udiv = udiv
; FULL16SIZE: cost of 4 for instruction: %srem = srem
; FULL16SIZE: cost of 4 for instruction: %urem = urem
; FULL16SIZE: cost of 1 for instruction: %mul = mul
; MIN16SIZE-LABEL: function 'constant_arithmetic'
; MIN16SIZE: cost of 4 for instruction: %sdiv = sdiv
; MIN16SIZE: cost of 4 for instruction: %udiv = udiv
; MIN16SIZE: cost of 4 for instruction: %srem = srem
; MIN16SIZE: cost of 4 for instruction: %urem = urem
; MIN16SIZE: cost of 1 for instruction: %mul = mul
; NANO16SIZE-LABEL: function 'constant_arithmetic'
; NANO16SIZE: cost of 4 for instruction: %sdiv = sdiv
; NANO16SIZE: cost of 4 for instruction: %udiv = udiv
; NANO16SIZE: cost of 4 for instruction: %srem = srem
; NANO16SIZE: cost of 4 for instruction: %urem = urem
; NANO16SIZE: cost of 1 for instruction: %mul = mul
; FULL32SIZE-LABEL: function 'constant_arithmetic'
; FULL32SIZE: cost of 4 for instruction: %sdiv = sdiv
; FULL32SIZE: cost of 4 for instruction: %udiv = udiv
; FULL32SIZE: cost of 4 for instruction: %srem = srem
; FULL32SIZE: cost of 4 for instruction: %urem = urem
; FULL32SIZE: cost of 1 for instruction: %mul = mul
; MIN32SIZE-LABEL: function 'constant_arithmetic'
; MIN32SIZE: cost of 4 for instruction: %sdiv = sdiv
; MIN32SIZE: cost of 4 for instruction: %udiv = udiv
; MIN32SIZE: cost of 4 for instruction: %srem = srem
; MIN32SIZE: cost of 4 for instruction: %urem = urem
; MIN32SIZE: cost of 1 for instruction: %mul = mul
  %sdiv = sdiv i16 %x, 3
  %udiv = udiv i16 %x, 3
  %srem = srem i16 %x, 3
  %urem = urem i16 %x, 3
  %mul = mul i16 %x, 7
  ret i16 %mul
}

; A helper-priced multiply plus a dependent add is not speculated by
; SimplifyCFG. Disabling the target arithmetic-call cost restores speculation.
; RUN: opt -mtriple=riscc -mcpu=min -codegen-opt-level=2 -passes=simplifycfg -S %s | FileCheck %s --check-prefix=CFG
; RUN: opt -mtriple=riscc -mcpu=min -codegen-opt-level=2 -riscc-arithmetic-call-cost=false -passes=simplifycfg -S %s \
; RUN:   | FileCheck %s --check-prefix=CFG-OFF
define i16 @guarded_mul(i16 %x, i16 %y, i1 %take) {
; CFG-LABEL: define i16 @guarded_mul
; CFG: br i1 %take,
; CFG: mul:
; CFG-NEXT: %product = mul i16 %x, %y
; CFG-NOT: select
; CFG-OFF-LABEL: define i16 @guarded_mul
; CFG-OFF-NOT: br i1
; CFG-OFF: select i1 %take
entry:
  br i1 %take, label %mul, label %done
mul:
  %product = mul i16 %x, %y
  %adjusted = add i16 %product, 1
  br label %done
done:
  %result = phi i16 [ 0, %entry ], [ %adjusted, %mul ]
  ret i16 %result
}
