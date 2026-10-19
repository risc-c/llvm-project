//===-- RISCCInstrInfo.td - RISCC Instructions -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "RISCCInstrFormats.td"

let SchedModel = RISCCSchedModel in {
def : InstRW<[WriteRISCCLoad],
             (instregex "^(LD|LD32|LDX|LDX32|LDB|LDB32|LDBS|LDBS32|LDH|LDHS|LDPC|LDPC_BRANCH)$")>;
def : InstRW<[WriteRISCCMul], (instregex "^(MUL|MUL32)$")>;
}

def : MnemonicAlias<"ldi8", "ldi">;
def : MnemonicAlias<"lui8", "lui">;
def : MnemonicAlias<"addi8", "addi">;
def : MnemonicAlias<"cmpi8", "cmpi">;
def : MnemonicAlias<"andi8", "andi">;
def : MnemonicAlias<"ori8", "ori">;
def : MnemonicAlias<"xori8", "xori">;
def : MnemonicAlias<"jal16", "jall">;
def : MnemonicAlias<"jmp16", "jmpl">;

def HasSysPred : Predicate<"Subtarget->hasSys()">,
                 AssemblerPredicate<(all_of FeatureSys)>;
def HasWideShiftPred : Predicate<"Subtarget->hasWideShift()">,
                      AssemblerPredicate<(all_of FeatureWideShift)>;
def HasIFLUSHPred : Predicate<"Subtarget->hasSys() && Subtarget->hasIFLUSH() && "
                             "!Subtarget->isNano()">,
                  AssemblerPredicate<(all_of FeatureSys, FeatureIFLUSH,
                                             (not FeatureNano))>;
def HasLongJallPred : Predicate<"Subtarget->hasLongJall()">,
                      AssemblerPredicate<(all_of FeatureLongJall)>;
def HasMulPred : Predicate<"Subtarget->hasMul()">,
                 AssemblerPredicate<(all_of FeatureMul)>;
def HasMduPred : Predicate<"Subtarget->hasMulhu() && Subtarget->hasDivu()">,
                 AssemblerPredicate<(all_of FeatureMdu)>;
def HasMulhuPred : Predicate<"Subtarget->hasMulhu()">,
                   AssemblerPredicate<(all_of FeatureMulhu)>;
def IsNanoPred : Predicate<"Subtarget->isNano()">,
                 AssemblerPredicate<(all_of FeatureNano)>;
def NoNanoPred : Predicate<"!Subtarget->isNano()">,
                 AssemblerPredicate<(not FeatureNano)>;
def IsRC32Pred : Predicate<"Subtarget->isRC32()">,
                 AssemblerPredicate<(all_of FeatureRC32)>;
def IsRC16Pred : Predicate<"!Subtarget->isRC32()">,
                 AssemblerPredicate<(not FeatureRC32)>;
def NoLongJallPred : Predicate<"!Subtarget->hasLongJall() && !Subtarget->isNano()">,
                     AssemblerPredicate<(all_of (not FeatureLongJall),
                                                (not FeatureNano))>;

def SDT_RISCCCall : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>] >;
def SDT_RISCCRet : SDTypeProfile<0, -1, []>;
def SDT_RISCCWrapper : SDTypeProfile<1, 1,
  [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;
def SDT_RISCCBrCC : SDTypeProfile<0, 4,
  [SDTCisSameAs<0, 1>, SDTCisVT<2, i16>, SDTCisVT<3, OtherVT>]>;
def SDT_RISCCSetCC : SDTypeProfile<1, 3,
  [SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>, SDTCisVT<3, i16>]>;
def SDT_RISCCSelectCC : SDTypeProfile<1, 5,
  [SDTCisSameAs<0, 3>, SDTCisSameAs<1, 2>, SDTCisSameAs<3, 4>,
   SDTCisVT<5, i16>]>;
// A single DAG opcode serves both widths. Keep its type generic so TableGen
// emits type checks when choosing the native-width instruction/pseudo.
def SDT_RISCCShift : SDTypeProfile<1, 2,
  [SDTCisInt<0>, SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>]>;

def RISCCcall : SDNode<"RISCCISD::CALL", SDT_RISCCCall,
  [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def RISCCcallPrivate : SDNode<"RISCCISD::CALL_PRIVATE", SDT_RISCCCall,
  [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def RISCCTail : SDNode<"RISCCISD::TAIL", SDT_RISCCCall,
  [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def RISCCTailPrivate : SDNode<"RISCCISD::TAIL_PRIVATE", SDT_RISCCCall,
  [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def RISCCret : SDNode<"RISCCISD::RET_FLAG", SDT_RISCCRet,
  [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def RISCCretPrivate : SDNode<"RISCCISD::RET_PRIVATE_FLAG", SDT_RISCCRet,
  [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def RISCCWrapper : SDNode<"RISCCISD::Wrapper", SDT_RISCCWrapper>;
def RISCCbrcc : SDNode<"RISCCISD::BR_CC", SDT_RISCCBrCC, [SDNPHasChain]>;
def RISCCbrccimm : SDNode<"RISCCISD::BR_CC_IMM", SDT_RISCCBrCC,
  [SDNPHasChain]>;
def RISCCsetcc : SDNode<"RISCCISD::SET_CC", SDT_RISCCSetCC>;
def RISCCsetccimm : SDNode<"RISCCISD::SET_CC_IMM", SDT_RISCCSetCC>;
def RISCCselectcc : SDNode<"RISCCISD::SELECT_CC", SDT_RISCCSelectCC>;
def RISCCselectccimm : SDNode<"RISCCISD::SELECT_CC_IMM", SDT_RISCCSelectCC>;
def RISCCshl : SDNode<"RISCCISD::SHL", SDT_RISCCShift>;
def RISCCsrl : SDNode<"RISCCISD::SRL", SDT_RISCCShift>;
def RISCCsra : SDNode<"RISCCISD::SRA", SDT_RISCCShift>;
def RISCCfsl1 : SDNode<"RISCCISD::FSL1", SDTIntBinOp>;
def RISCCfsr1 : SDNode<"RISCCISD::FSR1", SDTIntBinOp>;

def callseq_start : SDNode<"ISD::CALLSEQ_START", SDCallSeqStart<
  [SDTCisVT<0, i16>, SDTCisVT<1, i16>]>, [SDNPHasChain, SDNPOutGlue]>;
def callseq_end : SDNode<"ISD::CALLSEQ_END", SDCallSeqEnd<
  [SDTCisVT<0, i16>, SDTCisVT<1, i16>]>,
  [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def U8AsmOperand : AsmOperandClass {
  let Name = "U8Imm";
  let PredicateMethod = "isU8Imm";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidU8Imm";
}
def S8AsmOperand : AsmOperandClass {
  let Name = "S8Imm";
  let PredicateMethod = "isS8Imm";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidS8Imm";
}
def RC32WordDispAsmOperand : AsmOperandClass {
  let Name = "RC32WordDisp";
  let PredicateMethod = "isRC32WordDisp";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidRC32WordDisp";
}
def ShiftAsmOperand : AsmOperandClass {
  let Name = "ShiftImm";
  let PredicateMethod = "isShiftImm";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidShiftImm";
}
def U16AsmOperand : AsmOperandClass {
  let Name = "U16Imm";
  let PredicateMethod = "isU16Imm";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidU16Imm";
}

class RISCCU8Imm<ValueType vt>
    : Operand<vt>, ImmLeaf<vt, [{ return isUInt<8>(Imm); }]> {
  let ParserMatchClass = U8AsmOperand;
}
class RISCCS8Imm<ValueType vt>
    : Operand<vt>, ImmLeaf<vt, [{ return isInt<8>(Imm); }]> {
  let ParserMatchClass = S8AsmOperand;
  let DecoderMethod = "decodeSigned8";
}
def u8imm : RISCCU8Imm<i16>;
def s8imm : RISCCS8Imm<i16>;
def u8imm32 : RISCCU8Imm<i32>;
def s8imm32 : RISCCS8Imm<i32>;
def rc32worddisp : Operand<i32>, ImmLeaf<i32,
  [{ return isInt<9>(Imm) && !(Imm & 3); }]> {
  let ParserMatchClass = RC32WordDispAsmOperand;
  let EncoderMethod = "getRC32WordDispEncoding";
  let DecoderMethod = "decodeRC32WordDisp";
}
class RISCCShiftImm<ValueType vt> : Operand<vt>, ImmLeaf<vt,
  [{ return Imm >= 1 && Imm <= 8; }]> {
  let ParserMatchClass = ShiftAsmOperand;
  let EncoderMethod = "getShiftAmountEncoding";
  let DecoderMethod = "decodeShiftAmount";
}
def shiftimm : RISCCShiftImm<i16>;
def shiftimm32 : RISCCShiftImm<i32>;
def u16imm : Operand<i16>, ImmLeaf<i16, [{ return isUInt<16>(Imm); }]> {
  let ParserMatchClass = U16AsmOperand;
}
def brtarget : Operand<OtherVT> {
  let PrintMethod = "printBranchOperand";
  let EncoderMethod = "getBranchTargetEncoding";
  let DecoderMethod = "decodeBranch8";
}
def pcrel8word : Operand<i32> {
  let ParserMatchClass = S8AsmOperand;
  let PrintMethod = "printBranchOperand";
  let EncoderMethod = "getBranchTargetEncoding";
  let DecoderMethod = "decodeBranch8";
}
def codeaddr : Operand<i16> {
  let EncoderMethod = "getCodeTargetEncoding";
  let DecoderMethod = "decodeCodeTarget";
}
// Code-generation-only RC32 references to a native-width literal pool entry.
def cpaddr32 : Operand<i32>;
def condcode : Operand<i16>;

// LLVM needs distinct native-width register classes and instruction records,
// but their encodings, operands and selection patterns have one definition.
// Keep the width-specific types and feature predicates here. Instructions
// unique to a width or profile (LUI, LDPC, halfword accesses, Nano) stay explicit.
class NativeType<bit is32> {
  bit Is32 = is32;
  string Suffix = !if(is32, "32", "");
  ValueType VT = !if(is32, i32, i16);
  RegisterClass RC = !if(is32, GPR32, GPR);
  Operand SImm = !if(is32, s8imm32, s8imm);
  Operand UImm = !if(is32, u8imm32, u8imm);
  Operand ShiftImm = !if(is32, shiftimm32, shiftimm);
  Operand WordDisp = !if(is32, rc32worddisp, s8imm);
  list<Predicate> Predicates = !if(is32, [IsRC32Pred], []);
  list<Predicate> WidthPredicates = !if(is32, [IsRC32Pred], [IsRC16Pred]);
  list<Predicate> MainlinePredicates = !if(is32, [IsRC32Pred], [NoNanoPred]);
  string DecoderNamespace = !if(is32, "RC32", "");
}
def Native16 : NativeType<0>;
def Native32 : NativeType<1>;

// The same pairs also supply the C++ opcode mapping used by shared lowering
// and machine passes. An instruction's RC16 name is the width-neutral key.
class NativeInst<string opcode, NativeType w> {
  string NativeOpcode = opcode;
  bit IsRC32 = w.Is32;
}
def getRC32Opcode : InstrMapping {
  let FilterClass = "NativeInst";
  let RowFields = ["NativeOpcode"];
  let ColFields = ["IsRC32"];
  let KeyCol = ["0"];
  let ValueCols = [["1"]];
}

// Data operations have no effects beyond their explicit registers and memory
// operands. Custom-selected instructions need these flags too: without a DAG
// pattern TableGen otherwise treats them as unknown side effects.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {

// Loads and stores. Displacement ranges and scaling follow the native width.
foreach W = [Native16, Native32] in {
let Predicates = !if(W.Is32, [IsRC32Pred], [NoNanoPred, IsRC16Pred]),
    DecoderNamespace = !if(W.Is32, "RC32", "RC16") in {
let mayLoad = 1 in
def "LD"#W.Suffix : CompactRM<0, (outs W.RC:$rd),
    (ins W.RC:$base, W.WordDisp:$disp), "ld\t$rd, [$base + $disp]">,
    NativeInst<"LD", W>;
let mayStore = 1 in
def "ST"#W.Suffix : CompactRM<1, (outs),
    (ins W.RC:$rd, W.RC:$base, W.WordDisp:$disp),
    "st\t$rd, [$base + $disp]">, NativeInst<"ST", W>;
}
}
let Predicates = [IsNanoPred], DecoderNamespace = "Nano", mayLoad = 1 in
def LD_NANO : CompactRM<0, (outs GPR:$rd), (ins GPR:$base, s8imm:$disp),
                        "ld\t$rd, [$base + $disp]">;
let Predicates = [IsNanoPred], DecoderNamespace = "Nano", mayStore = 1 in
def ST_NANO : CompactRM<1, (outs), (ins GPR:$rd, GPR:$base, s8imm:$disp),
                        "st\t$rd, [$base + $disp]">;

// Immediate operations.
let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
foreach W = [Native16, Native32] in
let Predicates = W.Predicates, DecoderNamespace = W.DecoderNamespace in
def "LDI"#W.Suffix : RI<0b000, (outs W.RC:$rd), (ins W.UImm:$imm),
                       "ldi\t$rd, $imm">, NativeInst<"LDI", W>;
let Predicates = [IsRC16Pred] in
def LUI : RI<0b001, (outs GPR:$rd), (ins u8imm:$imm),
             "lui\t$rd, $imm">;
}
let Predicates = [IsRC32Pred], DecoderNamespace = "RC32", mayLoad = 1,
    mayStore = 0, hasSideEffects = 0, isReMaterializable = 1 in
def LDPC : RIPC<0b001, (outs GPR32:$rd), (ins pcrel8word:$target),
                 "ldpc\t$rd, $target">;
foreach W = [Native16, Native32] in {
let Predicates = W.Predicates, DecoderNamespace = W.DecoderNamespace,
    Constraints = "$rd = $src" in {
def "ADDI"#W.Suffix : RI<0b010, (outs W.RC:$rd), (ins W.RC:$src, W.SImm:$imm),
    "addi\t$rd, $imm", [(set W.VT:$rd, (add W.VT:$src, W.SImm:$imm))]>,
    NativeInst<"ADDI", W>;
def "ANDI"#W.Suffix : RI<0b100, (outs W.RC:$rd), (ins W.RC:$src, W.UImm:$imm),
    "andi\t$rd, $imm", [(set W.VT:$rd, (and W.VT:$src, W.UImm:$imm))]>,
    NativeInst<"ANDI", W>;
def "ORI"#W.Suffix : RI<0b101, (outs W.RC:$rd), (ins W.RC:$src, W.UImm:$imm),
    "ori\t$rd, $imm", [(set W.VT:$rd, (or W.VT:$src, W.UImm:$imm))]>,
    NativeInst<"ORI", W>;
def "XORI"#W.Suffix : RI<0b110, (outs W.RC:$rd), (ins W.RC:$src, W.UImm:$imm),
    "xori\t$rd, $imm", [(set W.VT:$rd, (xor W.VT:$src, W.UImm:$imm))]>,
    NativeInst<"XORI", W>;
}
let Predicates = !if(W.Is32, [IsRC32Pred], [IsRC16Pred, NoNanoPred]),
    DecoderNamespace = W.DecoderNamespace, Defs = [R0] in
def "CMPI"#W.Suffix : RI<0b011, (outs), (ins W.RC:$rd, W.SImm:$imm),
                        "cmpi\t$rd, $imm">, NativeInst<"CMPI", W>;
}

// Branches explicitly consume r0 so scheduling cannot separate a comparison
// from its branch with an r0-clobbering instruction.
let Uses = [R0], isBranch = 1, isTerminator = 1 in {
def BEQZ : RB<0b000, (outs), (ins brtarget:$target), "beqz\t$target">;
def BNEZ : RB<0b001, (outs), (ins brtarget:$target), "bnez\t$target">;
def BLTZ : RB<0b010, (outs), (ins brtarget:$target), "bltz\t$target">;
def BGEZ : RB<0b011, (outs), (ins brtarget:$target), "bgez\t$target">;
}
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def JMP8 : RB<0b100, (outs), (ins brtarget:$target), "jmp8\t$target",
              [(br bb:$target)]>;

// Three-register ALU operations.
foreach W = [Native16, Native32] in {
let Predicates = W.Predicates, DecoderNamespace = W.DecoderNamespace in {
let isCommutable = 1 in
def "ADD"#W.Suffix : RR<0x00, (outs W.RC:$rd), (ins W.RC:$ra, W.RC:$rb),
    "add\t$rd, $ra, $rb", [(set W.VT:$rd, (add W.VT:$ra, W.VT:$rb))]>,
    NativeInst<"ADD", W>;
def "SUB"#W.Suffix : RR<0x01, (outs W.RC:$rd), (ins W.RC:$ra, W.RC:$rb),
    "sub\t$rd, $ra, $rb", [(set W.VT:$rd, (sub W.VT:$ra, W.VT:$rb))]>,
    NativeInst<"SUB", W>;
let Predicates = W.MainlinePredicates in
def "SLT"#W.Suffix : RR<0x02, (outs W.RC:$rd), (ins W.RC:$ra, W.RC:$rb),
    "slt\t$rd, $ra, $rb">, NativeInst<"SLT", W>;
def "SLTU"#W.Suffix : RR<0x03, (outs W.RC:$rd), (ins W.RC:$ra, W.RC:$rb),
    "sltu\t$rd, $ra, $rb">, NativeInst<"SLTU", W>;
let isCommutable = 1 in {
def "AND"#W.Suffix : RR<0x04, (outs W.RC:$rd), (ins W.RC:$ra, W.RC:$rb),
    "and\t$rd, $ra, $rb", [(set W.VT:$rd, (and W.VT:$ra, W.VT:$rb))]>,
    NativeInst<"AND", W>;
def "OR"#W.Suffix : RR<0x05, (outs W.RC:$rd), (ins W.RC:$ra, W.RC:$rb),
    "or\t$rd, $ra, $rb", [(set W.VT:$rd, (or W.VT:$ra, W.VT:$rb))]>,
    NativeInst<"OR", W>;
def "XOR"#W.Suffix : RR<0x06, (outs W.RC:$rd), (ins W.RC:$ra, W.RC:$rb),
    "xor\t$rd, $ra, $rb", [(set W.VT:$rd, (xor W.VT:$ra, W.VT:$rb))]>,
    NativeInst<"XOR", W>;
let Predicates = !listconcat(W.Predicates, [HasMulPred]) in
def "MUL"#W.Suffix : RR<0x07, (outs W.RC:$rd), (ins W.RC:$ra, W.RC:$rb),
    "mul\t$rd, $ra, $rb", [(set W.VT:$rd, (mul W.VT:$ra, W.VT:$rb))]>,
    NativeInst<"MUL", W>;
}
}
}

// MDU writes architecturally paired results. The corresponding target DAG
// nodes select these directly; the tied inputs model the input register values
// that the ISA overwrites with the two results.
foreach W = [Native16, Native32] in {
let DecoderNamespace = W.DecoderNamespace in {
let Predicates = !listconcat(W.WidthPredicates, [HasMduPred]) in
def "DIVU"#W.Suffix : RRMdu<0x10, (outs W.RC:$rr, W.RC:$rq),
    (ins W.RC:$rr_in, W.RC:$rq_in, W.RC:$rb), "divu\t$rr, $rq, $rb">,
    NativeInst<"DIVU", W> {
  let Constraints = "$rr = $rr_in,$rq = $rq_in,@earlyclobber $rr,@earlyclobber $rq";
}
let Predicates = !listconcat(W.WidthPredicates, [HasMulhuPred]) in
def "MULHU"#W.Suffix : RRMdu<0x14, (outs W.RC:$rr, W.RC:$rq),
    (ins W.RC:$rq_in, W.RC:$rb), "mulhu\t$rr, $rq, $rb">,
    NativeInst<"MULHU", W> {
  let Constraints = "$rq = $rq_in,@earlyclobber $rr";
}
}
}

// Compact funnel shifts destructively shift rd while ra supplies the endpoint.
// They share f5=10_001; ooo=000 selects left and ooo=001 selects right.
foreach W = [Native16, Native32] in {
let Predicates = !if(W.Is32, [IsRC32Pred], [NoNanoPred, IsRC16Pred]),
    DecoderNamespace = W.DecoderNamespace, Constraints = "$rd = $rd_in" in {
def "FSL1"#!if(W.Is32, "_32", "")
    : RR<0x11, (outs W.RC:$rd), (ins W.RC:$rd_in, W.RC:$ra),
         "fsl1\t$rd, $ra",
         [(set W.VT:$rd, (RISCCfsl1 W.RC:$rd_in, W.RC:$ra))]>,
      NativeInst<"FSL1", W> {
  let rb = 0b000;
}
def "FSR1"#!if(W.Is32, "_32", "")
    : RR<0x11, (outs W.RC:$rd), (ins W.RC:$rd_in, W.RC:$ra),
         "fsr1\t$rd, $ra",
         [(set W.VT:$rd, (RISCCfsr1 W.RC:$rd_in, W.RC:$ra))]>,
      NativeInst<"FSR1", W> {
  let rb = 0b001;
}
}
}

foreach W = [Native16, Native32] in {
let Predicates = W.Predicates, DecoderNamespace = W.DecoderNamespace in {
let mayLoad = 1 in {
def "LDX"#W.Suffix : RR<0x08, (outs W.RC:$rd), (ins W.RC:$ra, W.RC:$rb),
    "ldx\t$rd, [$ra + $rb]">, NativeInst<"LDX", W>;
def "LDB"#W.Suffix : RRDirectA<0x0a, 0b000, (outs W.RC:$rd), (ins W.RC:$ra),
    "ldb\t$rd, [$ra]">, NativeInst<"LDB", W>;
let Predicates = W.MainlinePredicates in
def "LDBS"#W.Suffix : RRDirectA<0x0e, 0b000, (outs W.RC:$rd), (ins W.RC:$ra),
    "ldbs\t$rd, [$ra]">, NativeInst<"LDBS", W>;
}
let mayStore = 1 in
def "STB"#W.Suffix : RRDirectA<0x0b, 0b000, (outs), (ins W.RC:$rd, W.RC:$ra),
    "stb\t$rd, [$ra]">, NativeInst<"STB", W>;
}
}
let Predicates = [IsRC32Pred], mayLoad = 1 in {
def LDH : RRDirectA<0x0a, 0b010, (outs GPR32:$rd), (ins GPR32:$ra),
                    "ldh\t$rd, [$ra]">;
def LDHS : RRDirectA<0x0e, 0b010, (outs GPR32:$rd), (ins GPR32:$ra),
                     "ldhs\t$rd, [$ra]">;
}
let Predicates = [IsRC32Pred], mayStore = 1 in
def STH : RRDirectA<0x0b, 0b010, (outs), (ins GPR32:$rd, GPR32:$ra),
                    "sth\t$rd, [$ra]">;

foreach W = [Native16, Native32] in {
let Predicates = W.WidthPredicates, DecoderNamespace = W.DecoderNamespace in {
def "SRLI"#W.Suffix : RR<0x0c, (outs W.RC:$rd),
    (ins W.RC:$ra, W.ShiftImm:$amount), "srli\t$rd, $ra, $amount",
    [(set W.VT:$rd, (RISCCsrl W.RC:$ra, W.ShiftImm:$amount))]>,
    NativeInst<"SRLI", W> {
  bits<3> amount;
  let rb = amount;
}
def "SRAI"#W.Suffix : RR<0x0d, (outs W.RC:$rd),
    (ins W.RC:$ra, W.ShiftImm:$amount), "srai\t$rd, $ra, $amount",
    [(set W.VT:$rd, (RISCCsra W.RC:$ra, W.ShiftImm:$amount))]>,
    NativeInst<"SRAI", W> {
  bits<3> amount;
  let rb = amount;
}
let Predicates = !if(W.Is32, [IsRC32Pred], [IsRC16Pred, NoNanoPred]) in
def "SLLI"#W.Suffix : RR<0x0f, (outs W.RC:$rd),
    (ins W.RC:$ra, W.ShiftImm:$amount), "slli\t$rd, $ra, $amount",
    [(set W.VT:$rd, (RISCCshl W.RC:$ra, W.ShiftImm:$amount))]>,
    NativeInst<"SLLI", W> {
  bits<3> amount;
  let rb = amount;
}
}
}

} // Data operations.

// S-register and control instructions.
class SYS<bits<3> sub, dag outs, dag ins, string asm>
  : RISCCInst16<outs, ins, asm> {
  bits<3> sd;
  bits<3> ra;
  let Inst{15-14} = 0b11;
  let Inst{13-11} = sd;
  let Inst{10-8} = ra;
  let Inst{7-3} = 0x1f;
  let Inst{2-0} = sub;
}

let Predicates = [NoNanoPred], isReturn = 1, isTerminator = 1,
    isBarrier = 1 in
def RET : SYS<0, (outs), (ins SREG:$ra), "ret\t$ra"> { let sd = 0; }
let Predicates = [NoNanoPred], isBranch = 1, isIndirectBranch = 1,
    isTerminator = 1, isBarrier = 1 in
def JALR : SYS<1, (outs SREG:$sd), (ins GPR:$ra), "jalr\t$sd, $ra">;
// These are ordinary register transfers. Dependencies on the selected S
// register, including a saved return address, are explicit operands.
let Predicates = [NoNanoPred], hasSideEffects = 0, mayLoad = 0,
    mayStore = 0 in {
def MFS : SYS<2, (outs GPR:$sd), (ins SREG:$ra), "mfs\t$sd, $ra">;
def MTS : SYS<3, (outs SREG:$sd), (ins GPR:$ra), "mts\t$sd, $ra">;
}
let Predicates = [IsNanoPred], isBranch = 1, isIndirectBranch = 1,
    isTerminator = 1, isBarrier = 1, DecoderNamespace = "Nano" in
def JALR_NANO : SYS<1, (outs GPR:$sd), (ins GPR:$ra),
                    "jalr\t$sd, $ra">;
// Conventional spelling for the link-free JALR form.  The encoder selects
// S0 for mainline and r0 for Nano.
def JMP : MCPseudo<(outs), (ins GPR:$ra), "jmp\t$ra", 2>;
let Predicates = [HasSysPred], isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RETI : SYS<0, (outs), (ins SREG:$ra), "reti\t$ra"> { let sd = 5; }

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isReMaterializable = 1 in
def LDI16 : MCPseudo<(outs GPR:$rd), (ins u16imm:$imm),
                     "ldi16\t$rd, $imm", 4>;
let Predicates = [IsRC32Pred], mayLoad = 1, mayStore = 0,
    hasSideEffects = 0 in
def LDPC_BRANCH : Pseudo<(outs GPR32:$rd), (ins codeaddr:$target)> {
  let Size = 2;
}
def CONSTPOOL_ENTRY : Pseudo<(outs),
                            (ins cpaddr32:$label, cpaddr32:$value)> {
  let Size = 4;
  let isNotDuplicable = 1;
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
}
let Predicates = [HasLongJallPred] in
def JAL16 : RISCCInst32<(outs SREG:$sd), (ins codeaddr:$target),
                        "jall\t$sd, $target"> {
  bits<3> sd;
  bits<21> target;
  let Inst{15-14} = 0b00;
  let Inst{13-11} = sd;
  let Inst{10-6} = target{20-16};
  let Inst{5-0} = 0b110100;
  let Inst{31-16} = target{15-0};
}
let Predicates = [HasSysPred] in {
def CLI : SYS<0, (outs), (ins), "cli"> { let sd = 2; let ra = 0; }
def STI : SYS<0, (outs), (ins), "sti"> { let sd = 7; let ra = 0; }
}
// Optional frontend/cache flush; it has no architectural memory-ordering
// semantics and does not terminate control flow.
let Predicates = [HasIFLUSHPred] in
def IFLUSH : SYS<0, (outs), (ins), "iflush"> {
  let sd = 3;
  let ra = 0;
  let hasSideEffects = 1;
  let mayLoad = 1;
  let mayStore = 1;
}

// ABI-bound call/return forms remain compiler pseudos only. Source assembly
// must spell the link register and transfer instruction explicitly.
let Predicates = [HasLongJallPred], isCall = 1,
    Defs = [S7, R0] in
def CALL16 : Pseudo<(outs), (ins codeaddr:$target)> { let Size = 4; }
let Predicates = [NoLongJallPred], isCall = 1, isCodeGenOnly = 1,
    Defs = [S7, R0] in
def CALL_MIN : Pseudo<(outs), (ins codeaddr:$target)> {
  let Size = 6;
}
let Predicates = [IsNanoPred], isCall = 1, isCodeGenOnly = 1,
    Defs = [R6, R0] in
def CALL_NANO : Pseudo<(outs), (ins codeaddr:$target)> {
  let Size = 6;
}
foreach W = [Native16, Native32] in
let Predicates = W.MainlinePredicates, isCall = 1, Defs = [S7, R0] in
def "CALL"#W.Suffix : Pseudo<(outs), (ins W.RC:$target),
                            [(RISCCcall W.RC:$target)]>, NativeInst<"CALL", W> {
  let Size = 2;
}
let Predicates = [IsRC32Pred], isCall = 1,
    isCodeGenOnly = 1, Defs = [S7, R0] in
def CALL32_LITERAL : Pseudo<(outs), (ins cpaddr32:$target)> {
  let Size = 4;
}
let Predicates = [IsNanoPred], isCall = 1,
    Defs = [R6, R0] in
def CALL_NANO_REG : Pseudo<(outs), (ins GPR:$target),
                            [(RISCCcall GPR:$target)]> {
  let Size = 2;
}

// Direct-only private functions can use S3 instead of the public S7 ABI link.
// Keep that physical link in the instruction descriptor so ordinary
// call/return liveness remains exact.
multiclass PrivateLink<Register link> {
  let Predicates = [HasLongJallPred], isCall = 1, isCodeGenOnly = 1,
      Defs = [link, R0] in
  def _CALL16 : Pseudo<(outs), (ins codeaddr:$target)> {
    let Size = 4;
  }
  let Predicates = [NoLongJallPred], isCall = 1, isCodeGenOnly = 1,
      Defs = [link, R0] in
  def _CALL_MIN : Pseudo<(outs), (ins codeaddr:$target)> {
    let Size = 6;
  }
  let Predicates = [IsRC32Pred], isCall = 1, isCodeGenOnly = 1,
      Defs = [link, R0] in
  def _CALL32_LITERAL : Pseudo<(outs), (ins cpaddr32:$target)> {
    let Size = 4;
  }
  let Predicates = [HasLongJallPred], isCall = 1, isReturn = 1,
      isTerminator = 1, isBarrier = 1, isCodeGenOnly = 1,
      Uses = [link] in
  def _TAIL16 : Pseudo<(outs), (ins codeaddr:$target)> {
    let Size = 4;
  }
  let Predicates = [NoLongJallPred], isCall = 1, isReturn = 1,
      isTerminator = 1, isBarrier = 1, isCodeGenOnly = 1,
      Uses = [link] in
  def _TAIL_MIN : Pseudo<(outs), (ins codeaddr:$target)> {
    let Size = 6;
  }
  let Predicates = [IsRC32Pred], isCall = 1, isReturn = 1,
      isTerminator = 1, isBarrier = 1, isCodeGenOnly = 1,
      Uses = [link], Defs = [R0, S0] in
  def _TAIL32_LITERAL : Pseudo<(outs), (ins cpaddr32:$target)> {
    let Size = 4;
  }
  let Predicates = [NoNanoPred], isReturn = 1, isTerminator = 1,
      isBarrier = 1, isCodeGenOnly = 1, Uses = [link] in
  def _RET : Pseudo<(outs), (ins)> {
    let Size = 2;
  }
}

defm LINK_S3 : PrivateLink<S3>;

let isCall = 1, isReturn = 1, isTerminator = 1, isBarrier = 1 in {
let Predicates = [HasLongJallPred], Uses = [S7] in
def TAIL16 : Pseudo<(outs), (ins codeaddr:$target)> { let Size = 4; }
let Predicates = [NoLongJallPred], Uses = [S7], isCodeGenOnly = 1 in
def TAIL_MIN : Pseudo<(outs), (ins codeaddr:$target)> {
  let Size = 6;
}
let Predicates = [IsNanoPred], Uses = [R6], isCodeGenOnly = 1 in
def TAIL_NANO : Pseudo<(outs), (ins codeaddr:$target)> {
  let Size = 6;
}
foreach W = [Native16, Native32] in
let Predicates = W.MainlinePredicates, Uses = [S7] in
def "TAIL"#!if(W.Is32, "32", "_REG")
    : Pseudo<(outs), (ins W.RC:$target), [(RISCCTail W.RC:$target)]>,
      NativeInst<"TAIL_REG", W> {
  let Size = 2;
}
let Predicates = [IsRC32Pred], Uses = [S7], Defs = [R0, S0],
    isCodeGenOnly = 1 in
def TAIL32_LITERAL : Pseudo<(outs), (ins cpaddr32:$target)> {
  // LDPC followed by JALR. The referenced word lives in a normal RC32
  // literal pool and is deliberately not part of the instruction size.
  let Size = 4;
}
let Predicates = [IsNanoPred], Uses = [R6] in
def TAIL_NANO_REG : Pseudo<(outs), (ins GPR:$target),
                          [(RISCCTail GPR:$target)]> { let Size = 2; }
}

let Predicates = [HasLongJallPred], isBranch = 1, isTerminator = 1,
    isBarrier = 1 in
def JMP16 : MCPseudo<(outs), (ins codeaddr:$target), "jmpl\t$target", 4>;
let Predicates = [NoNanoPred], isReturn = 1, isTerminator = 1,
    isBarrier = 1, Uses = [S7] in
def RETS : Pseudo<(outs), (ins), [(RISCCret)]> {
  let Size = 2;
}
// Keep return semantics and the target's liveness until final emission. A
// reloaded/computed return address need not be copied through an S register.
let Predicates = [NoNanoPred], isReturn = 1, isTerminator = 1,
    isBarrier = 1, isCodeGenOnly = 1 in
def RET_REG : Pseudo<(outs), (ins GPR:$ra)> {
  let Size = 2;
}
let Predicates = [IsNanoPred], isReturn = 1, isTerminator = 1,
    isBarrier = 1 in
def RET_NANO : Pseudo<(outs), (ins GPR:$ra), [(RISCCret GPR:$ra)]> {
  let Size = 2;
}
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isMoveReg = 1 in {
foreach W = [Native16, Native32] in
let Predicates = W.WidthPredicates in
def "MOV"#W.Suffix : MCPseudo<(outs W.RC:$rd), (ins W.RC:$rs),
                             "mov\t$rd, $rs", 2>, NativeInst<"MOV", W>;
}
def NOP : MCPseudo<(outs), (ins), "nop", 2>;
let isTerminator = 1, isBarrier = 1 in
def HALT : MCPseudo<(outs), (ins), "halt", 2>;

// Code-generation pseudos.
let Defs = [R0, R7], Uses = [R7], hasSideEffects = 1 in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
  [(callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
  [(callseq_end timm:$amt1, timm:$amt2)]>;
}
// A small frame address expands to MOV and, when needed, ADDI.
// Eligibility and expansion cost are checked by isReMaterializableImpl.
let Size = 4, hasSideEffects = 0, mayLoad = 0, mayStore = 0,
    isReMaterializable = 1 in {
foreach W = [Native16, Native32] in
def "FRAMEADDR"#W.Suffix : Pseudo<(outs W.RC:$dst),
                                (ins i16imm:$fi, i16imm:$offset)>,
                           NativeInst<"FRAMEADDR", W>;
}

// Register byte values need sign extension even when signed loads exist.
// Keep it tied until after register allocation, then use ordinary immediates.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
foreach W = [Native16, Native32] in
let Predicates = W.WidthPredicates, Constraints = "$dst = $src" in
def "SEXT8_RC"#!if(W.Is32, "32", "16")
    : Pseudo<(outs W.RC:$dst), (ins W.RC:$src),
             !if(W.Is32, [], [(set W.VT:$dst, (sext_inreg W.RC:$src, i8))])>,
      NativeInst<"SEXT8_RC16", W> {
  let Size = 6;
}

let Predicates = [IsRC32Pred], Constraints = "$dst = $src", Defs = [R0] in
def SEXT16_RC32 : Pseudo<(outs GPR32:$dst), (ins GPR32:$src)> {
  let Size = 10;
}
let Predicates = [IsRC32Pred, HasWideShiftPred] in
def SEXT16_SHIFT_RC32 : Pseudo<(outs GPR32:$dst), (ins GPR32:$src)> {
  let Size = 8;
}
}

foreach W = [Native16, Native32] in {
let usesCustomInserter = 1 in {
let isBranch = 1, isTerminator = 1 in {
def "PseudoBRCC"#W.Suffix : Pseudo<(outs),
    (ins W.RC:$lhs, W.RC:$rhs, condcode:$cc, brtarget:$target),
    [(RISCCbrcc W.RC:$lhs, W.RC:$rhs, timm:$cc, bb:$target)]>,
    NativeInst<"PseudoBRCC", W>;
def "PseudoBRCCImm"#W.Suffix : Pseudo<(outs),
    (ins W.RC:$lhs, W.SImm:$rhs, condcode:$cc, brtarget:$target),
    [(RISCCbrccimm W.RC:$lhs, timm:$rhs, timm:$cc, bb:$target)]>,
    NativeInst<"PseudoBRCCImm", W>;
}
def "PseudoSETCC"#W.Suffix : Pseudo<(outs W.RC:$dst),
    (ins W.RC:$lhs, W.RC:$rhs, condcode:$cc),
    [(set W.VT:$dst, (RISCCsetcc W.RC:$lhs, W.RC:$rhs, timm:$cc))]>,
    NativeInst<"PseudoSETCC", W>;
def "PseudoSETCCImm"#W.Suffix : Pseudo<(outs W.RC:$dst),
    (ins W.RC:$lhs, W.UImm:$rhs, condcode:$cc),
    [(set W.VT:$dst, (RISCCsetccimm W.RC:$lhs, timm:$rhs, timm:$cc))]>,
    NativeInst<"PseudoSETCCImm", W>;
def "PseudoSELECTCCImm"#W.Suffix : Pseudo<(outs W.RC:$dst),
    (ins W.RC:$lhs, W.SImm:$rhs, W.RC:$truev, W.RC:$falsev, condcode:$cc),
    [(set W.VT:$dst, (RISCCselectccimm W.RC:$lhs, timm:$rhs, W.RC:$truev,
                                     W.RC:$falsev, timm:$cc))]>,
    NativeInst<"PseudoSELECTCCImm", W>;
def "PseudoSELECTCC"#W.Suffix : Pseudo<(outs W.RC:$dst),
    (ins W.RC:$lhs, W.RC:$rhs, W.RC:$truev, W.RC:$falsev, condcode:$cc),
    [(set W.VT:$dst, (RISCCselectcc W.RC:$lhs, W.RC:$rhs, W.RC:$truev,
                                  W.RC:$falsev, timm:$cc))]>,
    NativeInst<"PseudoSELECTCC", W>;
let Predicates = W.Predicates in {
def "PseudoSHL"#W.Suffix : Pseudo<(outs W.RC:$dst), (ins W.RC:$src, W.RC:$amount),
    [(set W.VT:$dst, (RISCCshl W.RC:$src, W.RC:$amount))]>,
    NativeInst<"PseudoSHL", W>;
def "PseudoSRL"#W.Suffix : Pseudo<(outs W.RC:$dst), (ins W.RC:$src, W.RC:$amount),
    [(set W.VT:$dst, (RISCCsrl W.RC:$src, W.RC:$amount))]>,
    NativeInst<"PseudoSRL", W>;
def "PseudoSRA"#W.Suffix : Pseudo<(outs W.RC:$dst), (ins W.RC:$src, W.RC:$amount),
    [(set W.VT:$dst, (RISCCsra W.RC:$src, W.RC:$amount))]>,
    NativeInst<"PseudoSRA", W>;
}
}
}

def : Pat<(i16 (RISCCWrapper tglobaladdr:$addr)), (LDI16 tglobaladdr:$addr)>;
def : Pat<(i16 (RISCCWrapper tglobaltlsaddr:$addr)),
          (LDI16 tglobaltlsaddr:$addr)>;
def : Pat<(i16 (RISCCWrapper texternalsym:$addr)),
          (LDI16 texternalsym:$addr)>;
def : Pat<(i16 imm:$imm), (LDI16 imm:$imm)>;
let Predicates = [HasLongJallPred] in {
def : Pat<(RISCCcall (i16 tglobaladdr:$target)),
          (CALL16 tglobaladdr:$target)>;
def : Pat<(RISCCcall (i16 texternalsym:$target)),
          (CALL16 texternalsym:$target)>;
def : Pat<(RISCCTail (i16 tglobaladdr:$target)),
          (TAIL16 tglobaladdr:$target)>;
def : Pat<(RISCCTail (i16 texternalsym:$target)),
          (TAIL16 texternalsym:$target)>;
def : Pat<(RISCCcallPrivate (i16 tglobaladdr:$target)),
          (LINK_S3_CALL16 tglobaladdr:$target)>;
def : Pat<(RISCCTailPrivate (i16 tglobaladdr:$target)),
          (LINK_S3_TAIL16 tglobaladdr:$target)>;
}
let Predicates = [NoLongJallPred] in {
def : Pat<(RISCCcall (i16 tglobaladdr:$target)),
          (CALL_MIN tglobaladdr:$target)>;
def : Pat<(RISCCcall (i16 texternalsym:$target)),
          (CALL_MIN texternalsym:$target)>;
def : Pat<(RISCCTail (i16 tglobaladdr:$target)),
          (TAIL_MIN tglobaladdr:$target)>;
def : Pat<(RISCCTail (i16 texternalsym:$target)),
          (TAIL_MIN texternalsym:$target)>;
def : Pat<(RISCCcallPrivate (i16 tglobaladdr:$target)),
          (LINK_S3_CALL_MIN tglobaladdr:$target)>;
def : Pat<(RISCCTailPrivate (i16 tglobaladdr:$target)),
          (LINK_S3_TAIL_MIN tglobaladdr:$target)>;
}
let Predicates = [IsNanoPred] in {
def : Pat<(RISCCcall (i16 tglobaladdr:$target)),
          (CALL_NANO tglobaladdr:$target)>;
def : Pat<(RISCCcall (i16 texternalsym:$target)),
          (CALL_NANO texternalsym:$target)>;
def : Pat<(RISCCTail (i16 tglobaladdr:$target)),
          (TAIL_NANO tglobaladdr:$target)>;
def : Pat<(RISCCTail (i16 texternalsym:$target)),
          (TAIL_NANO texternalsym:$target)>;
}
def : Pat<(RISCCretPrivate), (LINK_S3_RET)>;

// Keep direct targets visible to register-usage analysis until pool placement.
let Predicates = [IsRC32Pred] in {
def : Pat<(RISCCcall (i32 tglobaladdr:$target)),
          (CALL32_LITERAL tglobaladdr:$target)>;
def : Pat<(RISCCcall (i32 texternalsym:$target)),
          (CALL32_LITERAL texternalsym:$target)>;
def : Pat<(RISCCTail (i32 tglobaladdr:$target)),
          (TAIL32_LITERAL tglobaladdr:$target)>;
def : Pat<(RISCCTail (i32 texternalsym:$target)),
          (TAIL32_LITERAL texternalsym:$target)>;
def : Pat<(RISCCcallPrivate (i32 tglobaladdr:$target)),
          (LINK_S3_CALL32_LITERAL tglobaladdr:$target)>;
def : Pat<(RISCCTailPrivate (i32 tglobaladdr:$target)),
          (LINK_S3_TAIL32_LITERAL tglobaladdr:$target)>;
}
