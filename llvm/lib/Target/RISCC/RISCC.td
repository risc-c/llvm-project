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
  "Enable interrupt and system instructions">;
def FeatureLongJall : SubtargetFeature<"jall", "HasLongJall", "true",
  "Enable the two-word JALL/JMPL control transfers">;
def FeatureWideShift : SubtargetFeature<"wide-shift", "HasWideShift", "true",
  "Enable shift counts from 2 through 8">;
def FeatureMul : SubtargetFeature<"mul", "HasMul", "true",
  "Enable the low-half multiply instruction">;
def FeatureMulhu : SubtargetFeature<"mulhu", "HasMulhu", "true",
  "Enable paired unsigned multiply-high/low">;
def FeatureMdu : SubtargetFeature<"mdu", "HasDivu", "true",
  "Enable paired unsigned multiply-high/low and divide/remainder",
  [FeatureMul, FeatureMulhu]>;
def FeatureNano : SubtargetFeature<"nano", "IsNano", "true",
  "Select the incompatible Nano register and instruction profile">;
def FeatureRC32 : SubtargetFeature<"rc32", "IsRC32", "true",
  "Select the 32-bit RISC-C base ISA">;

def FeatureEarlyBranches : SubtargetFeature<"early-branches", "HasEarlyBranches", "true",
  "Schedule independent work before Decode-stage conditional branches">;

include "RISCCSchedule.td"
include "RISCCInstrInfo.td"

// Tune the shared ISA profiles for the pipeline. This only reorders existing
// instructions; multicycle implementations need no different instruction set.
def : ProcessorModel<"min", RISCCSchedModel, [FeatureEarlyBranches]>;
def : ProcessorModel<"nano", NoSchedModel, [FeatureNano]>;
def : ProcessorModel<"sys", RISCCSchedModel,
                     [FeatureSys, FeatureLongJall, FeatureEarlyBranches]>;
def : ProcessorModel<"full", RISCCSchedModel,
                     [FeatureSys, FeatureLongJall, FeatureWideShift, FeatureMul,
                      FeatureEarlyBranches]>;

def RISCCInstrInfo : InstrInfo;

def RISCCAsmParser : AsmParser {
  let AllowDuplicateRegisterNames = 0;
}

def RISCCAsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
  int PassSubtarget = 1;
}

def RISCC : Target {
  let AllowRegisterRenaming = 1;
  let InstructionSet = RISCCInstrInfo;
  let AssemblyParsers = [RISCCAsmParser];
  let AssemblyWriters = [RISCCAsmWriter];
}
