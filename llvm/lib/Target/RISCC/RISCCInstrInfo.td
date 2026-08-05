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
def : MnemonicAlias<"ldp", "ldph">;

def HasSysPred : Predicate<"Subtarget->hasSys()">,
                 AssemblerPredicate<(all_of FeatureSys)>;
def HasMulPred : Predicate<"Subtarget->hasMul()">,
                 AssemblerPredicate<(all_of FeatureMul)>;
def HasMduPred : Predicate<"Subtarget->hasMulhu() && Subtarget->hasDivu()">,
                 AssemblerPredicate<(all_of FeatureMdu)>;
def IsNanoPred : Predicate<"Subtarget->isNano()">,
                 AssemblerPredicate<(all_of FeatureNano)>;
def NoNanoPred : Predicate<"!Subtarget->isNano()">,
                 AssemblerPredicate<(not FeatureNano)>;
def IsMinPred : Predicate<"!Subtarget->hasSys() && !Subtarget->isNano()">,
                AssemblerPredicate<(all_of (not FeatureSys),
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
  let DecoderMethod = "decodeSigned8";
}
def codeaddr : Operand<i16> {
  let EncoderMethod = "getCodeTargetEncoding";
  let DecoderMethod = "decodeCodeTarget";
}
def condcode : Operand<i16>;

// Loads and stores.  Word displacements are signed bytes.
let Predicates = [NoNanoPred], mayLoad = 1 in
def LD : CompactRM<0, (outs GPR:$rd), (ins GPR:$base, s8imm:$disp),
                    "ld\t$rd, [$base + $disp]">;
let Predicates = [NoNanoPred], mayStore = 1 in
def ST : CompactRM<1, (outs), (ins GPR:$rd, GPR:$base, s8imm:$disp),
                    "st\t$rd, [$base + $disp]">;
let Predicates = [IsNanoPred], DecoderNamespace = "Nano", mayLoad = 1 in
def LD_NANO : CompactRM<0, (outs GPR:$rd), (ins GPR:$base, s8imm:$disp),
                        "ld\t$rd, [$base + $disp]">;
let Predicates = [IsNanoPred], DecoderNamespace = "Nano", mayStore = 1 in
def ST_NANO : CompactRM<1, (outs), (ins GPR:$rd, GPR:$base, s8imm:$disp),
                        "st\t$rd, [$base + $disp]">;

// Immediate operations.
def LDI : RI<0b000, (outs GPR:$rd), (ins u8imm:$imm),
             "ldi\t$rd, $imm">;
def LUI : RI<0b001, (outs GPR:$rd), (ins u8imm:$imm),
             "lui\t$rd, $imm">;
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

// MDU writes architecturally paired results. The corresponding target DAG
// nodes select these directly; the tied inputs model the input register values
// that the ISA overwrites with the two results.
let Predicates = [HasMduPred], hasSideEffects = 1 in {
def DIVU : RRMdu<0x10, (outs GPR:$rr, GPR:$rq),
             (ins GPR:$rr_in, GPR:$rq_in, GPR:$rb),
             "divu\t$rr, $rq, $rb"> {
  let Constraints = "$rr = $rr_in,$rq = $rq_in,@earlyclobber $rr,@earlyclobber $rq";
}
def MULHU : RRMdu<0x14, (outs GPR:$rr, GPR:$rq),
              (ins GPR:$rq_in, GPR:$rb), "mulhu\t$rr, $rq, $rb"> {
  let Constraints = "$rq = $rq_in,@earlyclobber $rr";
}
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
let Predicates = [NoNanoPred] in
def LDPH : RRDirectA<0x0a, 0b011, (outs GPR:$rd), (ins GPR:$ra),
                     "ldph\t$rd, [$ra]">;
def LDB : RRDirectA<0x0a, 0b000, (outs GPR:$rd), (ins GPR:$ra),
                    "ldb\t$rd, [$ra]">;
let Predicates = [NoNanoPred] in
def LDBS : RRDirectA<0x0e, 0b000, (outs GPR:$rd), (ins GPR:$ra),
                     "ldbs\t$rd, [$ra]">;
}
let mayStore = 1 in
def STB : RRDirectA<0x0b, 0b000, (outs), (ins GPR:$rd, GPR:$ra),
                    "stb\t$rd, [$ra]">;

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
let Predicates = [HasSysPred], isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RETI : SYS<0, (outs), (ins SREG:$ra), "reti\t$ra"> { let sd = 5; }

def LDI16 : MCPseudo<(outs GPR:$rd), (ins u16imm:$imm),
                     "ldi16\t$rd, $imm", 4>;
let Predicates = [HasSysPred] in
def JAL16 : RISCCInst32<(outs SREG:$sd), (ins codeaddr:$target),
                        "jal16\t$sd, $target"> {
  bits<3> sd;
  bits<15> target;
  let Inst{15-14} = 0b00;
  let Inst{13-11} = sd;
  let Inst{10-8} = 0b111;
  let Inst{7-0} = 0;
  let Inst{30-16} = target;
  let Inst{31} = 0;
}
let Predicates = [HasSysPred] in {
def CLI : SYS<0, (outs), (ins), "cli"> { let sd = 2; let ra = 0; }
def STI : SYS<0, (outs), (ins), "sti"> { let sd = 7; let ra = 0; }
}

// Assembler and code-generation aliases are expanded by the MC emitter.
def LI : MCPseudo<(outs GPR:$rd), (ins u16imm:$imm), "li\t$rd, $imm", 4>;
let Predicates = [HasSysPred], isCall = 1,
    Defs = [S7, R0, R1, R2, R3, R4] in
def CALL16 : MCPseudo<(outs), (ins codeaddr:$target), "call16\t$target", 4>;
let Predicates = [IsMinPred], isCall = 1, isCodeGenOnly = 1,
    Defs = [S7, R0, R1, R2, R3, R4] in
def CALL_MIN : Pseudo<(outs), (ins codeaddr:$target)> {
  let Size = 6;
}
let Predicates = [IsNanoPred], isCall = 1, isCodeGenOnly = 1,
    Defs = [R6, R0, R1, R2, R3, R4] in
def CALL_NANO : Pseudo<(outs), (ins codeaddr:$target)> {
  let Size = 6;
}
let Predicates = [NoNanoPred], isCall = 1,
    Defs = [S7, R0, R1, R2, R3, R4] in
def CALL : MCPseudo<(outs), (ins GPR:$target), "call\t$target", 2> {
  let Pattern = [(RISCCcall GPR:$target)];
}
let Predicates = [IsNanoPred], isCall = 1,
    Defs = [R6, R0, R1, R2, R3, R4] in
def CALL_NANO_REG : MCPseudo<(outs), (ins GPR:$target),
                             "call\t$target", 2> {
  let Pattern = [(RISCCcall GPR:$target)];
}

// Direct-only private functions use S3 instead of the public S7 ABI link.
// Keep that physical link in the instruction descriptor so ordinary
// call/return liveness remains exact.
multiclass PrivateLink<Register link> {
  let Predicates = [HasSysPred], isCall = 1, isCodeGenOnly = 1,
      Defs = [link, R0, R1, R2, R3, R4] in
  def _CALL16 : Pseudo<(outs), (ins codeaddr:$target)> {
    let Size = 4;
  }
  let Predicates = [IsMinPred], isCall = 1, isCodeGenOnly = 1,
      Defs = [link, R0, R1, R2, R3, R4] in
  def _CALL_MIN : Pseudo<(outs), (ins codeaddr:$target)> {
    let Size = 6;
  }
  let Predicates = [HasSysPred], isCall = 1, isReturn = 1,
      isTerminator = 1, isBarrier = 1, isCodeGenOnly = 1,
      Uses = [link] in
  def _TAIL16 : Pseudo<(outs), (ins codeaddr:$target)> {
    let Size = 4;
  }
  let Predicates = [IsMinPred], isCall = 1, isReturn = 1,
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
let Predicates = [HasSysPred], Uses = [S7] in
def TAIL16 : MCPseudo<(outs), (ins codeaddr:$target),
                      "tail\t$target", 4>;
let Predicates = [IsMinPred], Uses = [S7], isCodeGenOnly = 1 in
def TAIL_MIN : Pseudo<(outs), (ins codeaddr:$target)> {
  let Size = 6;
}
let Predicates = [IsNanoPred], Uses = [R6], isCodeGenOnly = 1 in
def TAIL_NANO : Pseudo<(outs), (ins codeaddr:$target)> {
  let Size = 6;
}
// Register-tail aliases remain available to assembly, but code generation
// deliberately limits sibling calls to direct targets.
let Predicates = [NoNanoPred], Uses = [S7] in
def TAIL_REG : MCPseudo<(outs), (ins GPR:$target),
                        "tail\t$target", 2>;
let Predicates = [IsNanoPred], Uses = [R6] in
def TAIL_NANO_REG : MCPseudo<(outs), (ins GPR:$target),
                             "tail\t$target", 2>;
}
let Predicates = [HasSysPred], isBranch = 1, isTerminator = 1,
    isBarrier = 1 in
def JMP16 : MCPseudo<(outs), (ins codeaddr:$target), "jmp16\t$target", 4>;
let Predicates = [NoNanoPred], isReturn = 1, isTerminator = 1,
    isBarrier = 1, Uses = [S7] in
def RETS : MCPseudo<(outs), (ins), "rets", 2> {
  let Pattern = [(RISCCret)];
}
let Predicates = [IsNanoPred], isReturn = 1, isTerminator = 1,
    isBarrier = 1 in
def RET_NANO : MCPseudo<(outs), (ins GPR:$ra), "ret\t$ra", 2> {
  let Pattern = [(RISCCret GPR:$ra)];
}
def MOV : MCPseudo<(outs GPR:$rd), (ins GPR:$rs), "mov\t$rd, $rs", 2>;
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
  [(RISCCbrcc GPR:$lhs, GPR:$rhs, imm:$cc, bb:$target)]>;
let usesCustomInserter = 1, isBranch = 1, isTerminator = 1 in
def PseudoBRCCImm : Pseudo<(outs),
  (ins GPR:$lhs, s8imm:$rhs, condcode:$cc, brtarget:$target),
  [(RISCCbrccimm GPR:$lhs, s8imm:$rhs, imm:$cc, bb:$target)]>;
let usesCustomInserter = 1 in
def PseudoSETCC : Pseudo<(outs GPR:$dst),
  (ins GPR:$lhs, GPR:$rhs, condcode:$cc),
  [(set i16:$dst, (RISCCsetcc GPR:$lhs, GPR:$rhs, imm:$cc))]>;
let usesCustomInserter = 1 in
def PseudoSETCCImm : Pseudo<(outs GPR:$dst),
  (ins GPR:$lhs, u8imm:$rhs, condcode:$cc),
  [(set i16:$dst, (RISCCsetccimm GPR:$lhs, u8imm:$rhs, imm:$cc))]>;
let usesCustomInserter = 1 in
def PseudoSELECTCC : Pseudo<(outs GPR:$dst),
  (ins GPR:$lhs, GPR:$rhs, GPR:$truev, GPR:$falsev, condcode:$cc),
  [(set i16:$dst, (RISCCselectcc GPR:$lhs, GPR:$rhs, GPR:$truev,
                                     GPR:$falsev, imm:$cc))]>;
let usesCustomInserter = 1 in {
def PseudoSHL : Pseudo<(outs GPR:$dst), (ins GPR:$src, GPR:$amount),
  [(set i16:$dst, (RISCCshl GPR:$src, GPR:$amount))]>;
def PseudoSRL : Pseudo<(outs GPR:$dst), (ins GPR:$src, GPR:$amount),
  [(set i16:$dst, (RISCCsrl GPR:$src, GPR:$amount))]>;
def PseudoSRA : Pseudo<(outs GPR:$dst), (ins GPR:$src, GPR:$amount),
  [(set i16:$dst, (RISCCsra GPR:$src, GPR:$amount))]>;
}

def : Pat<(i16 (RISCCWrapper tglobaladdr:$addr)), (LI tglobaladdr:$addr)>;
def : Pat<(i16 (RISCCWrapper tglobaltlsaddr:$addr)),
          (LI tglobaltlsaddr:$addr)>;
def : Pat<(i16 (RISCCWrapper texternalsym:$addr)), (LI texternalsym:$addr)>;
def : Pat<(i16 imm:$imm), (LI imm:$imm)>;
let Predicates = [HasSysPred] in {
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
let Predicates = [IsMinPred] in {
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
