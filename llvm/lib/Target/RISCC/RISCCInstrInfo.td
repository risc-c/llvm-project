//===-- RISCCInstrInfo.td - RISCC Instructions -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "RISCCInstrFormats.td"

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
def SDT_RISCCShift : SDTypeProfile<1, 2,
  [SDTCisSameAs<0, 1>, SDTCisVT<0, i16>, SDTCisVT<2, i16>]>;

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
def RISCCshl : SDNode<"RISCCISD::SHL", SDT_RISCCShift>;
def RISCCsrl : SDNode<"RISCCISD::SRL", SDT_RISCCShift>;
def RISCCsra : SDNode<"RISCCISD::SRA", SDT_RISCCShift>;
def RISCCfsl1 : SDNode<"RISCCISD::FSL1", SDT_RISCCShift>;
def RISCCfsr1 : SDNode<"RISCCISD::FSR1", SDT_RISCCShift>;

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

def u8imm : Operand<i16>, ImmLeaf<i16, [{ return isUInt<8>(Imm); }]> {
  let ParserMatchClass = U8AsmOperand;
}
def s8imm : Operand<i16>, ImmLeaf<i16, [{ return isInt<8>(Imm); }]> {
  let ParserMatchClass = S8AsmOperand;
  let DecoderMethod = "decodeSigned8";
}
def u8imm32 : Operand<i32>, ImmLeaf<i32, [{ return isUInt<8>(Imm); }]> {
  let ParserMatchClass = U8AsmOperand;
}
def s8imm32 : Operand<i32>, ImmLeaf<i32, [{ return isInt<8>(Imm); }]> {
  let ParserMatchClass = S8AsmOperand;
  let DecoderMethod = "decodeSigned8";
}
def rc32worddisp : Operand<i32>, ImmLeaf<i32,
  [{ return isInt<9>(Imm) && !(Imm & 3); }]> {
  let ParserMatchClass = RC32WordDispAsmOperand;
  let EncoderMethod = "getRC32WordDispEncoding";
  let DecoderMethod = "decodeRC32WordDisp";
}
def shiftimm : Operand<i16>, ImmLeaf<i16,
  [{ return Imm >= 1 && Imm <= 8; }]> {
  let ParserMatchClass = ShiftAsmOperand;
  let EncoderMethod = "getShiftAmountEncoding";
  let DecoderMethod = "decodeShiftAmount";
}
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
def condcode : Operand<i16>;

// Loads and stores.  Word displacements are signed bytes.
let Predicates = [NoNanoPred, IsRC16Pred], DecoderNamespace = "RC16",
    mayLoad = 1 in
def LD : CompactRM<0, (outs GPR:$rd), (ins GPR:$base, s8imm:$disp),
                    "ld\t$rd, [$base + $disp]">;
let Predicates = [NoNanoPred, IsRC16Pred], DecoderNamespace = "RC16",
    mayStore = 1 in
def ST : CompactRM<1, (outs), (ins GPR:$rd, GPR:$base, s8imm:$disp),
                    "st\t$rd, [$base + $disp]">;
let Predicates = [IsNanoPred], DecoderNamespace = "Nano", mayLoad = 1 in
def LD_NANO : CompactRM<0, (outs GPR:$rd), (ins GPR:$base, s8imm:$disp),
                        "ld\t$rd, [$base + $disp]">;
let Predicates = [IsNanoPred], DecoderNamespace = "Nano", mayStore = 1 in
def ST_NANO : CompactRM<1, (outs), (ins GPR:$rd, GPR:$base, s8imm:$disp),
                        "st\t$rd, [$base + $disp]">;
let Predicates = [IsRC32Pred], DecoderNamespace = "RC32", mayLoad = 1 in
def LD32 : CompactRM32<0, (outs GPR32:$rd),
                        (ins GPR32:$base, rc32worddisp:$disp),
                        "ld\t$rd, [$base + $disp]">;
let Predicates = [IsRC32Pred], DecoderNamespace = "RC32", mayStore = 1 in
def ST32 : CompactRM32<1, (outs),
                        (ins GPR32:$rd, GPR32:$base, rc32worddisp:$disp),
                        "st\t$rd, [$base + $disp]">;

// Immediate operations.
def LDI : RI<0b000, (outs GPR:$rd), (ins u8imm:$imm),
             "ldi\t$rd, $imm">;
let Predicates = [IsRC32Pred], DecoderNamespace = "RC32" in
def LDI32 : RI<0b000, (outs GPR32:$rd), (ins u8imm32:$imm),
                "ldi\t$rd, $imm">;
let Predicates = [IsRC16Pred], DecoderNamespace = "RC16" in
def LUI : RI<0b001, (outs GPR:$rd), (ins u8imm:$imm),
             "lui\t$rd, $imm">;
let Predicates = [IsRC32Pred], DecoderNamespace = "RC32", mayLoad = 1 in
def LDPC : RIPC<0b001, (outs GPR32:$rd), (ins pcrel8word:$target),
                 "ldpc\t$rd, $target">;
let Constraints = "$rd = $src" in {
def ADDI : RI<0b010, (outs GPR:$rd), (ins GPR:$src, s8imm:$imm),
              "addi\t$rd, $imm", [(set i16:$rd, (add i16:$src, s8imm:$imm))]>;
def ANDI : RI<0b100, (outs GPR:$rd), (ins GPR:$src, u8imm:$imm),
              "andi\t$rd, $imm", [(set i16:$rd, (and i16:$src, u8imm:$imm))]>;
def ORI : RI<0b101, (outs GPR:$rd), (ins GPR:$src, u8imm:$imm),
             "ori\t$rd, $imm", [(set i16:$rd, (or i16:$src, u8imm:$imm))]>;
def XORI : RI<0b110, (outs GPR:$rd), (ins GPR:$src, u8imm:$imm),
              "xori\t$rd, $imm", [(set i16:$rd, (xor i16:$src, u8imm:$imm))]>;
}
let Predicates = [IsRC32Pred], DecoderNamespace = "RC32",
    Constraints = "$rd = $src" in {
def ADDI32 : RI<0b010, (outs GPR32:$rd), (ins GPR32:$src, s8imm32:$imm),
                "addi\t$rd, $imm",
                [(set i32:$rd, (add i32:$src, s8imm32:$imm))]>;
def ANDI32 : RI<0b100, (outs GPR32:$rd), (ins GPR32:$src, u8imm32:$imm),
                "andi\t$rd, $imm",
                [(set i32:$rd, (and i32:$src, u8imm32:$imm))]>;
def ORI32 : RI<0b101, (outs GPR32:$rd), (ins GPR32:$src, u8imm32:$imm),
               "ori\t$rd, $imm",
               [(set i32:$rd, (or i32:$src, u8imm32:$imm))]>;
def XORI32 : RI<0b110, (outs GPR32:$rd), (ins GPR32:$src, u8imm32:$imm),
                "xori\t$rd, $imm",
                [(set i32:$rd, (xor i32:$src, u8imm32:$imm))]>;
}
let Predicates = [NoNanoPred], Defs = [R0] in
def CMPI : RI<0b011, (outs), (ins GPR:$rd, s8imm:$imm),
              "cmpi\t$rd, $imm">;

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
def ADD : RR<0x00, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
             "add\t$rd, $ra, $rb", [(set i16:$rd, (add i16:$ra, i16:$rb))]>;
def SUB : RR<0x01, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
             "sub\t$rd, $ra, $rb", [(set i16:$rd, (sub i16:$ra, i16:$rb))]>;
let Predicates = [NoNanoPred] in
def SLT : RR<0x02, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
             "slt\t$rd, $ra, $rb">;
def SLTU : RR<0x03, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
              "sltu\t$rd, $ra, $rb">;
let isCommutable = 1 in {
def AND : RR<0x04, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
             "and\t$rd, $ra, $rb", [(set i16:$rd, (and i16:$ra, i16:$rb))]>;
def OR : RR<0x05, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
            "or\t$rd, $ra, $rb", [(set i16:$rd, (or i16:$ra, i16:$rb))]>;
def XOR : RR<0x06, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
             "xor\t$rd, $ra, $rb", [(set i16:$rd, (xor i16:$ra, i16:$rb))]>;
let Predicates = [HasMulPred] in
def MUL : RR<0x07, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
             "mul\t$rd, $ra, $rb", [(set i16:$rd, (mul i16:$ra, i16:$rb))]>;
}
let Predicates = [IsRC32Pred], DecoderNamespace = "RC32" in {
def ADD32 : RR<0x00, (outs GPR32:$rd), (ins GPR32:$ra, GPR32:$rb),
               "add\t$rd, $ra, $rb",
               [(set i32:$rd, (add i32:$ra, i32:$rb))]>;
def SUB32 : RR<0x01, (outs GPR32:$rd), (ins GPR32:$ra, GPR32:$rb),
               "sub\t$rd, $ra, $rb",
               [(set i32:$rd, (sub i32:$ra, i32:$rb))]>;
def SLT32 : RR<0x02, (outs GPR32:$rd), (ins GPR32:$ra, GPR32:$rb),
               "slt\t$rd, $ra, $rb">;
def SLTU32 : RR<0x03, (outs GPR32:$rd), (ins GPR32:$ra, GPR32:$rb),
                "sltu\t$rd, $ra, $rb">;
let isCommutable = 1 in {
def AND32 : RR<0x04, (outs GPR32:$rd), (ins GPR32:$ra, GPR32:$rb),
               "and\t$rd, $ra, $rb",
               [(set i32:$rd, (and i32:$ra, i32:$rb))]>;
def OR32 : RR<0x05, (outs GPR32:$rd), (ins GPR32:$ra, GPR32:$rb),
              "or\t$rd, $ra, $rb",
              [(set i32:$rd, (or i32:$ra, i32:$rb))]>;
def XOR32 : RR<0x06, (outs GPR32:$rd), (ins GPR32:$ra, GPR32:$rb),
               "xor\t$rd, $ra, $rb",
               [(set i32:$rd, (xor i32:$ra, i32:$rb))]>;
}
}

// MDU writes architecturally paired results. The corresponding target DAG
// nodes select these directly; the tied inputs model the input register values
// that the ISA overwrites with the two results.
let Predicates = [HasMduPred], hasSideEffects = 1 in
def DIVU : RRMdu<0x10, (outs GPR:$rr, GPR:$rq),
             (ins GPR:$rr_in, GPR:$rq_in, GPR:$rb),
             "divu\t$rr, $rq, $rb"> {
  let Constraints = "$rr = $rr_in,$rq = $rq_in,@earlyclobber $rr,@earlyclobber $rq";
}
let Predicates = [HasMulhuPred], hasSideEffects = 1 in
def MULHU : RRMdu<0x14, (outs GPR:$rr, GPR:$rq),
              (ins GPR:$rq_in, GPR:$rb), "mulhu\t$rr, $rq, $rb"> {
  let Constraints = "$rq = $rq_in,@earlyclobber $rr";
}

// Compact funnel shifts destructively shift rd while ra supplies the endpoint.
// They share f5=10_001; ooo=000 selects left and ooo=001 selects right.
let Predicates = [NoNanoPred], Constraints = "$rd = $rd_in" in {
def FSL1 : RR<0x11, (outs GPR:$rd), (ins GPR:$rd_in, GPR:$ra),
              "fsl1\t$rd, $ra",
              [(set i16:$rd, (RISCCfsl1 GPR:$rd_in, GPR:$ra))]> {
  let rb = 0b000;
}
def FSR1 : RR<0x11, (outs GPR:$rd), (ins GPR:$rd_in, GPR:$ra),
              "fsr1\t$rd, $ra",
              [(set i16:$rd, (RISCCfsr1 GPR:$rd_in, GPR:$ra))]> {
  let rb = 0b001;
}
}

let mayLoad = 1 in {
def LDX : RR<0x08, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
              "ldx\t$rd, [$ra + $rb]">;
def LDB : RRDirectA<0x0a, 0b000, (outs GPR:$rd), (ins GPR:$ra),
                    "ldb\t$rd, [$ra]">;
let Predicates = [NoNanoPred] in
def LDBS : RRDirectA<0x0e, 0b000, (outs GPR:$rd), (ins GPR:$ra),
                     "ldbs\t$rd, [$ra]">;
}
let Predicates = [IsRC32Pred], DecoderNamespace = "RC32", mayLoad = 1 in {
def LDX32 : RR<0x08, (outs GPR32:$rd), (ins GPR32:$ra, GPR32:$rb),
                "ldx\t$rd, [$ra + $rb]">;
def LDB32 : RRDirectA<0x0a, 0b000, (outs GPR32:$rd), (ins GPR32:$ra),
                     "ldb\t$rd, [$ra]">;
def LDBS32 : RRDirectA<0x0e, 0b000, (outs GPR32:$rd), (ins GPR32:$ra),
                      "ldbs\t$rd, [$ra]">;
}
let mayStore = 1 in
def STB : RRDirectA<0x0b, 0b000, (outs), (ins GPR:$rd, GPR:$ra),
                    "stb\t$rd, [$ra]">;
let Predicates = [IsRC32Pred], DecoderNamespace = "RC32", mayStore = 1 in
def STB32 : RRDirectA<0x0b, 0b000, (outs), (ins GPR32:$rd, GPR32:$ra),
                     "stb\t$rd, [$ra]">;
let Predicates = [IsRC32Pred], mayLoad = 1 in {
def LDH : RRDirectA<0x0a, 0b010, (outs GPR32:$rd), (ins GPR32:$ra),
                    "ldh\t$rd, [$ra]">;
def LDHS : RRDirectA<0x0e, 0b010, (outs GPR32:$rd), (ins GPR32:$ra),
                     "ldhs\t$rd, [$ra]">;
}
let Predicates = [IsRC32Pred], mayStore = 1 in
def STH : RRDirectA<0x0b, 0b010, (outs), (ins GPR32:$rd, GPR32:$ra),
                    "sth\t$rd, [$ra]">;

def SRLI : RR<0x0c, (outs GPR:$rd), (ins GPR:$ra, shiftimm:$amount),
              "srli\t$rd, $ra, $amount",
              [(set i16:$rd, (RISCCsrl GPR:$ra, shiftimm:$amount))]> {
  bits<3> amount;
  let rb = amount;
}
def SRAI : RR<0x0d, (outs GPR:$rd), (ins GPR:$ra, shiftimm:$amount),
              "srai\t$rd, $ra, $amount",
              [(set i16:$rd, (RISCCsra GPR:$ra, shiftimm:$amount))]> {
  bits<3> amount;
  let rb = amount;
}
let Predicates = [NoNanoPred] in
def SLLI : RR<0x0f, (outs GPR:$rd), (ins GPR:$ra, shiftimm:$amount),
              "slli\t$rd, $ra, $amount",
              [(set i16:$rd, (RISCCshl GPR:$ra, shiftimm:$amount))]> {
  bits<3> amount;
  let rb = amount;
}

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
let Predicates = [NoNanoPred] in {
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

def LDI16 : MCPseudo<(outs GPR:$rd), (ins u16imm:$imm),
                     "ldi16\t$rd, $imm", 4>;
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

// ABI-bound call/return forms remain compiler pseudos only. Source assembly
// must spell the link register and transfer instruction explicitly.
let Predicates = [HasLongJallPred], isCall = 1,
    Defs = [S7, R0, R1, R2, R3] in
def CALL16 : Pseudo<(outs), (ins codeaddr:$target)> { let Size = 4; }
let Predicates = [NoLongJallPred], isCall = 1, isCodeGenOnly = 1,
    Defs = [S7, R0, R1, R2, R3] in
def CALL_MIN : Pseudo<(outs), (ins codeaddr:$target)> {
  let Size = 6;
}
let Predicates = [IsNanoPred], isCall = 1, isCodeGenOnly = 1,
    Defs = [R6, R0, R1, R2, R3] in
def CALL_NANO : Pseudo<(outs), (ins codeaddr:$target)> {
  let Size = 6;
}
let Predicates = [NoNanoPred], isCall = 1,
    Defs = [S7, R0, R1, R2, R3] in
def CALL : Pseudo<(outs), (ins GPR:$target), [(RISCCcall GPR:$target)]> {
  let Size = 2;
}
let Predicates = [IsRC32Pred], isCall = 1,
    Defs = [S7, R0, R1, R2, R3] in
def CALL32 : Pseudo<(outs), (ins GPR32:$target),
                    [(RISCCcall GPR32:$target)]> {
  let Size = 2;
}
let Predicates = [IsNanoPred], isCall = 1,
    Defs = [R6, R0, R1, R2, R3] in
def CALL_NANO_REG : Pseudo<(outs), (ins GPR:$target),
                            [(RISCCcall GPR:$target)]> {
  let Size = 2;
}

// Direct-only private functions use S3 instead of the public S7 ABI link.
// Keep that physical link in the instruction descriptor so ordinary
// call/return liveness remains exact.
multiclass PrivateLink<Register link> {
  let Predicates = [HasLongJallPred], isCall = 1, isCodeGenOnly = 1,
      Defs = [link, R0, R1, R2, R3] in
  def _CALL16 : Pseudo<(outs), (ins codeaddr:$target)> {
    let Size = 4;
  }
  let Predicates = [NoLongJallPred], isCall = 1, isCodeGenOnly = 1,
      Defs = [link, R0, R1, R2, R3] in
  def _CALL_MIN : Pseudo<(outs), (ins codeaddr:$target)> {
    let Size = 6;
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
let Predicates = [NoNanoPred], Uses = [S7] in
def TAIL_REG : Pseudo<(outs), (ins GPR:$target)> { let Size = 2; }
let Predicates = [IsRC32Pred], Uses = [S7] in
def TAIL32 : Pseudo<(outs), (ins GPR32:$target),
                    [(RISCCTail GPR32:$target)]> { let Size = 2; }
let Predicates = [IsNanoPred], Uses = [R6] in
def TAIL_NANO_REG : Pseudo<(outs), (ins GPR:$target)> { let Size = 2; }
}

let Predicates = [HasLongJallPred], isBranch = 1, isTerminator = 1,
    isBarrier = 1 in
def JMP16 : MCPseudo<(outs), (ins codeaddr:$target), "jmpl\t$target", 4>;
let Predicates = [NoNanoPred], isReturn = 1, isTerminator = 1,
    isBarrier = 1, Uses = [S7] in
def RETS : Pseudo<(outs), (ins), [(RISCCret)]> {
  let Size = 2;
}
let Predicates = [IsNanoPred], isReturn = 1, isTerminator = 1,
    isBarrier = 1 in
def RET_NANO : Pseudo<(outs), (ins GPR:$ra), [(RISCCret GPR:$ra)]> {
  let Size = 2;
}
let Predicates = [IsRC16Pred] in
def MOV : MCPseudo<(outs GPR:$rd), (ins GPR:$rs), "mov\t$rd, $rs", 2>;
let Predicates = [IsRC32Pred] in
def MOV32 : MCPseudo<(outs GPR32:$rd), (ins GPR32:$rs), "mov\t$rd, $rs", 2>;
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
def FRAMEADDR : Pseudo<(outs GPR:$dst), (ins i16imm:$fi, i16imm:$offset)>;
def FRAMEADDR32 : Pseudo<(outs GPR32:$dst),
                         (ins i16imm:$fi, i16imm:$offset)>;

// Nano has unsigned byte loads. Keep sign extension as one tied pseudo until
// after register allocation, then expand it to three ordinary immediates.
let Predicates = [IsNanoPred], Constraints = "$dst = $src" in
def SEXT8_NANO : Pseudo<(outs GPR:$dst), (ins GPR:$src),
  [(set i16:$dst, (sext_inreg GPR:$src, i8))]> {
  let Size = 6;
}

let usesCustomInserter = 1, isBranch = 1, isTerminator = 1 in
def PseudoBRCC : Pseudo<(outs),
  (ins GPR:$lhs, GPR:$rhs, condcode:$cc, brtarget:$target),
  [(RISCCbrcc GPR:$lhs, GPR:$rhs, timm:$cc, bb:$target)]>;
let usesCustomInserter = 1, isBranch = 1, isTerminator = 1 in
def PseudoBRCCImm : Pseudo<(outs),
  (ins GPR:$lhs, s8imm:$rhs, condcode:$cc, brtarget:$target),
  [(RISCCbrccimm GPR:$lhs, s8imm:$rhs, timm:$cc, bb:$target)]>;
let usesCustomInserter = 1 in
def PseudoSETCC : Pseudo<(outs GPR:$dst),
  (ins GPR:$lhs, GPR:$rhs, condcode:$cc),
  [(set i16:$dst, (RISCCsetcc GPR:$lhs, GPR:$rhs, timm:$cc))]>;
let usesCustomInserter = 1 in
def PseudoSETCCImm : Pseudo<(outs GPR:$dst),
  (ins GPR:$lhs, u8imm:$rhs, condcode:$cc),
  [(set i16:$dst, (RISCCsetccimm GPR:$lhs, u8imm:$rhs, timm:$cc))]>;
let usesCustomInserter = 1 in
def PseudoSELECTCC : Pseudo<(outs GPR:$dst),
  (ins GPR:$lhs, GPR:$rhs, GPR:$truev, GPR:$falsev, condcode:$cc),
  [(set i16:$dst, (RISCCselectcc GPR:$lhs, GPR:$rhs, GPR:$truev,
                                     GPR:$falsev, timm:$cc))]>;
let usesCustomInserter = 1, isBranch = 1, isTerminator = 1 in
def PseudoBRCC32 : Pseudo<(outs),
  (ins GPR32:$lhs, GPR32:$rhs, condcode:$cc, brtarget:$target),
  [(RISCCbrcc GPR32:$lhs, GPR32:$rhs, timm:$cc, bb:$target)]>;
let usesCustomInserter = 1 in
def PseudoSETCC32 : Pseudo<(outs GPR32:$dst),
  (ins GPR32:$lhs, GPR32:$rhs, condcode:$cc),
  [(set i32:$dst, (RISCCsetcc GPR32:$lhs, GPR32:$rhs, timm:$cc))]>;
let usesCustomInserter = 1 in
def PseudoSELECTCC32 : Pseudo<(outs GPR32:$dst),
  (ins GPR32:$lhs, GPR32:$rhs, GPR32:$truev, GPR32:$falsev, condcode:$cc),
  [(set i32:$dst, (RISCCselectcc GPR32:$lhs, GPR32:$rhs, GPR32:$truev,
                                     GPR32:$falsev, timm:$cc))]>;
let usesCustomInserter = 1 in {
def PseudoSHL : Pseudo<(outs GPR:$dst), (ins GPR:$src, GPR:$amount),
  [(set i16:$dst, (RISCCshl GPR:$src, GPR:$amount))]>;
def PseudoSRL : Pseudo<(outs GPR:$dst), (ins GPR:$src, GPR:$amount),
  [(set i16:$dst, (RISCCsrl GPR:$src, GPR:$amount))]>;
def PseudoSRA : Pseudo<(outs GPR:$dst), (ins GPR:$src, GPR:$amount),
  [(set i16:$dst, (RISCCsra GPR:$src, GPR:$amount))]>;
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
