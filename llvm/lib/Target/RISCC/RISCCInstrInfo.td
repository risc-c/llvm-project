include "RISCCInstrFormats.td"

def HasSysPred : Predicate<"Subtarget->hasSys()">,
                 AssemblerPredicate<(all_of FeatureSys)>;
def HasMulPred : Predicate<"Subtarget->hasMul()">,
                 AssemblerPredicate<(all_of FeatureMul)>;

def SDT_RISCCCall : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>] >;
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
def RISCCret : SDNode<"RISCCISD::RET_FLAG", SDTNone,
  [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def RISCCWrapper : SDNode<"RISCCISD::Wrapper", SDT_RISCCWrapper>;
def RISCCbrcc : SDNode<"RISCCISD::BR_CC", SDT_RISCCBrCC, [SDNPHasChain]>;
def RISCCsetcc : SDNode<"RISCCISD::SET_CC", SDT_RISCCSetCC>;
def RISCCselectcc : SDNode<"RISCCISD::SELECT_CC", SDT_RISCCSelectCC>;
def RISCCshl : SDNode<"RISCCISD::SHL", SDT_RISCCShift>;
def RISCCsrl : SDNode<"RISCCISD::SRL", SDT_RISCCShift>;
def RISCCsra : SDNode<"RISCCISD::SRA", SDT_RISCCShift>;

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
}
def shiftimm : Operand<i16>, ImmLeaf<i16,
  [{ return Imm >= 1 && Imm <= 8; }]> {
  let ParserMatchClass = ShiftAsmOperand;
}
def u16imm : Operand<i16>, ImmLeaf<i16, [{ return isUInt<16>(Imm); }]> {
  let ParserMatchClass = U16AsmOperand;
}
def brtarget : Operand<OtherVT> {
  let PrintMethod = "printBranchOperand";
}
def codeaddr : Operand<i16>;
def condcode : Operand<i16>;

// Loads and stores.  Word displacements are signed bytes.
let mayLoad = 1 in
def LDW : RM<0b00, (outs GPR:$rd), (ins GPR:$base, s8imm:$disp),
             "ldw\t$rd, [$base + $disp]">;
let mayStore = 1 in
def STW : RM<0b01, (outs), (ins GPR:$rd, GPR:$base, s8imm:$disp),
             "stw\t$rd, [$base + $disp]">;

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
let Defs = [R0] in
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

let mayLoad = 1 in {
def LDWX : RR<0x08, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
              "ldwx\t$rd, [$ra + $rb]">;
def LDB : RR<0x0a, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
             "ldb\t$rd, [$ra + $rb]">;
def LDBS : RR<0x0e, (outs GPR:$rd), (ins GPR:$ra, GPR:$rb),
              "ldbs\t$rd, [$ra + $rb]">;
}
let mayStore = 1 in
def STB : RR<0x0b, (outs), (ins GPR:$rd, GPR:$ra),
             "stb\t$rd, [$ra]"> {
  let rb = 0;
}

def SHRI : RR<0x0c, (outs GPR:$rd), (ins GPR:$ra, shiftimm:$amount),
              "shri\t$rd, $ra, $amount",
              [(set i16:$rd, (RISCCsrl GPR:$ra, shiftimm:$amount))]>;
def SARI : RR<0x0d, (outs GPR:$rd), (ins GPR:$ra, shiftimm:$amount),
              "sari\t$rd, $ra, $amount",
              [(set i16:$rd, (RISCCsra GPR:$ra, shiftimm:$amount))]>;
def SHLI : RR<0x0f, (outs GPR:$rd), (ins GPR:$ra, shiftimm:$amount),
              "shli\t$rd, $ra, $amount",
              [(set i16:$rd, (RISCCshl GPR:$ra, shiftimm:$amount))]>;

// S-register and control instructions.
class SYS<bits<3> sub, dag outs, dag ins, string asm>
  : RISCCInst<outs, ins, asm> {
  bits<3> sd = 0;
  bits<3> ra = 0;
  let Inst{15-14} = 0b11;
  let Inst{13-11} = sd;
  let Inst{10-8} = ra;
  let Inst{7-3} = 0x1f;
  let Inst{2-0} = sub;
}

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RET : SYS<0, (outs), (ins SREG:$ra), "ret\t$ra">;
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def JAL : SYS<1, (outs SREG:$sd), (ins GPR:$ra), "jal\t$sd, $ra">;
def MFS : SYS<2, (outs GPR:$sd), (ins SREG:$ra), "mfs\t$sd, $ra">;
def MTS : SYS<3, (outs SREG:$sd), (ins GPR:$ra), "mts\t$sd, $ra">;
let Predicates = [HasSysPred], isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RETI : SYS<4, (outs), (ins SREG:$ra), "reti\t$ra">;

let Predicates = [HasSysPred] in
def JAL16 : SYS<5, (outs SREG:$sd), (ins codeaddr:$target),
                "jal16\t$sd, $target"> {
  let Size = 4;
  let ra = 0;
}
let Predicates = [HasSysPred] in {
def CLI : SYS<6, (outs), (ins), "cli"> { let sd = 0; let ra = 0; }
def STI : SYS<7, (outs), (ins), "sti"> { let sd = 0; let ra = 0; }
}

// Multi-instruction assembler aliases are represented as concrete MC opcodes.
def LI : RISCCInst<(outs GPR:$rd), (ins u16imm:$imm), "li\t$rd, $imm"> {
  let Size = 4;
}
let Predicates = [HasSysPred], isCall = 1,
    Defs = [S7, R0, R1, R2, R3, R4] in
def CALL16 : RISCCInst<(outs), (ins codeaddr:$target), "call16\t$target",
                       []> {
  let Size = 4;
}
let isCall = 1, Defs = [S7, R0, R1, R2, R3, R4] in
def CALL : RISCCInst<(outs), (ins GPR:$target), "call\t$target",
                     [(RISCCcall GPR:$target)]>;
let Predicates = [HasSysPred], isBranch = 1, isTerminator = 1,
    isBarrier = 1 in
def JMP16 : RISCCInst<(outs), (ins codeaddr:$target), "jmp16\t$target"> {
  let Size = 4;
}
let isReturn = 1, isTerminator = 1, isBarrier = 1, Uses = [S7] in
def RETS : RISCCInst<(outs), (ins), "rets", [(RISCCret)]>;
def MOV : RISCCInst<(outs GPR:$rd), (ins GPR:$rs), "mov\t$rd, $rs">;
def NOP : RISCCInst<(outs), (ins), "nop">;
let isTerminator = 1, isBarrier = 1 in
def HALT : RISCCInst<(outs), (ins), "halt">;

// Code-generation pseudos.
let Defs = [R0, R7], Uses = [R7], hasSideEffects = 1 in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
  [(callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
  [(callseq_end timm:$amt1, timm:$amt2)]>;
}
def FRAMEADDR : Pseudo<(outs GPR:$dst), (ins i16imm:$fi, i16imm:$offset)>;
let Predicates = [HasSysPred], Uses = [R0], isBranch = 1,
    isTerminator = 1, isCodeGenOnly = 1 in
def LONG_BR : RISCCInst<(outs), (ins condcode:$cc, brtarget:$target), ""> {
  let Size = 6;
}

let usesCustomInserter = 1, isBranch = 1, isTerminator = 1 in
def PseudoBRCC : Pseudo<(outs),
  (ins GPR:$lhs, GPR:$rhs, condcode:$cc, brtarget:$target),
  [(RISCCbrcc GPR:$lhs, GPR:$rhs, imm:$cc, bb:$target)]>;
let usesCustomInserter = 1 in
def PseudoSETCC : Pseudo<(outs GPR:$dst),
  (ins GPR:$lhs, GPR:$rhs, condcode:$cc),
  [(set i16:$dst, (RISCCsetcc GPR:$lhs, GPR:$rhs, imm:$cc))]>;
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
def : Pat<(RISCCcall (i16 tglobaladdr:$target)),
          (CALL16 tglobaladdr:$target)>;
def : Pat<(RISCCcall (i16 texternalsym:$target)),
          (CALL16 texternalsym:$target)>;
