//===-- RISCCCallingConv.td - RISCC Calling Conventions ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Both ABIs use the same return registers and native-word stack slots.
// Only the promoted type and the slot size/alignment depend on XLEN.
multiclass NativeCallingConv<ValueType vt, list<ValueType> narrow, int bytes> {
  def "RetCC_"#NAME : CallingConv<[
    CCIfType<narrow, CCPromoteToType<vt>>,
    CCIfType<[vt], CCAssignToReg<[R1, R2, R3]>>
  ]>;
  def "CC_"#NAME#"_Stack" : CallingConv<[
    CCIfType<narrow, CCPromoteToType<vt>>,
    CCIfType<[vt], CCAssignToStack<bytes, bytes>>
  ]>;
}

defm RISCC : NativeCallingConv<i16, [i1, i8], 2>;
defm RISCC32 : NativeCallingConv<i32, [i1, i8, i16], 4>;
