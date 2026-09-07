//===-- RISCCCallingConv.td - RISCC Calling Conventions ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def RetCC_RISCC : CallingConv<[
  CCIfType<[i1, i8], CCPromoteToType<i16>>,
  CCIfType<[i16], CCAssignToReg<[R1, R2, R3]>>
]>;
def RetCC_RISCC32 : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[R1, R2, R3]>>
]>;

def CC_RISCC_Stack : CallingConv<[
  CCIfType<[i1, i8], CCPromoteToType<i16>>,
  CCIfType<[i16], CCAssignToStack<2, 2>>
]>;
def CC_RISCC32_Stack : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToStack<4, 4>>
]>;
