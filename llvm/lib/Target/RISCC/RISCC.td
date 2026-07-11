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

include "RISCCInstrInfo.td"

def : ProcessorModel<"min", NoSchedModel, []>;
def : ProcessorModel<"sys", NoSchedModel, [FeatureSys, FeatureWideShift]>;
def : ProcessorModel<"full", NoSchedModel,
                     [FeatureSys, FeatureWideShift, FeatureMul]>;

def RISCCInstrInfo : InstrInfo;

def RISCCAsmParser : AsmParser {
  let ShouldEmitMatchRegisterAltName = 1;
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
