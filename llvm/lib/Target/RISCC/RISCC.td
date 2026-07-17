//===-- RISCC.td - RISCC Target Definition -----------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

include "RISCCRegisterInfo.td"
include "RISCCCallingConv.td"
defm : RemapAllTargetPseudoPointerOperands<GPR>;

def FeatureSys : SubtargetFeature<"sys", "HasSys", "true",
  "Enable JAL16 and system instructions">;
def FeatureWideShift : SubtargetFeature<"wide-shift", "HasWideShift", "true",
  "Enable shift counts from 2 through 8">;
def FeatureMul : SubtargetFeature<"mul", "HasMul", "true",
  "Enable the low-half multiply instruction">;
def FeatureNano : SubtargetFeature<"nano", "IsNano", "true",
  "Select the incompatible Nano register and instruction profile">;

include "RISCCInstrInfo.td"

def : ProcessorModel<"min", NoSchedModel, []>;
def : ProcessorModel<"nano", NoSchedModel, [FeatureNano]>;
def : ProcessorModel<"sys", NoSchedModel, [FeatureSys, FeatureWideShift]>;
def : ProcessorModel<"full", NoSchedModel,
                     [FeatureSys, FeatureWideShift, FeatureMul]>;

def RISCCInstrInfo : InstrInfo;

def RISCCAsmParser : AsmParser {
  let AllowDuplicateRegisterNames = 0;
}

def RISCCAsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
  int PassSubtarget = 1;
}

def RISCC : Target {
  let InstructionSet = RISCCInstrInfo;
  let AssemblyParsers = [RISCCAsmParser];
  let AssemblyWriters = [RISCCAsmWriter];
}
