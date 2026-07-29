//===-- RISCCInstrFormats.td - RISCC Instruction Formats ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class RISCCInst<dag outs, dag ins, string asm, list<dag> pattern = []>
  : Instruction {
  let Namespace = "RISCC";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asm;
  let Pattern = pattern;
}

class RISCCInst16<dag outs, dag ins, string asm, list<dag> pattern = []>
  : RISCCInst<outs, ins, asm, pattern> {
  field bits<16> Inst;
  let Size = 2;
}

class RISCCInst32<dag outs, dag ins, string asm, list<dag> pattern = []>
  : RISCCInst<outs, ins, asm, pattern> {
  field bits<32> Inst;
  let Size = 4;
}

class RI<bits<3> opc, dag outs, dag ins, string asm,
         list<dag> pattern = []> : RISCCInst16<outs, ins, asm, pattern> {
  bits<3> rd;
  bits<8> imm;
  let Inst{15-14} = 0b10;
  let Inst{13-11} = rd;
  let Inst{10-8} = opc;
  let Inst{7-0} = imm;
}

class RR<bits<5> func, dag outs, dag ins, string asm,
         list<dag> pattern = []> : RISCCInst16<outs, ins, asm, pattern> {
  bits<3> rd;
  bits<3> ra;
  bits<3> rb;
  let Inst{15-14} = 0b11;
  let Inst{13-11} = rd;
  let Inst{10-8} = ra;
  let Inst{7-3} = func;
  let Inst{2-0} = rb;
}

// Paired-result MDU instructions use the ordinary three-register encoding,
// but their fields name the architectural result roles rather than a generic
// single destination/source pair.
class RRMdu<bits<5> func, dag outs, dag ins, string asm,
            list<dag> pattern = []> : RISCCInst16<outs, ins, asm, pattern> {
  bits<3> rr;
  bits<3> rq;
  bits<3> rb;
  let Inst{15-14} = 0b11;
  let Inst{13-11} = rr;
  let Inst{10-8} = rq;
  let Inst{7-3} = func;
  let Inst{2-0} = rb;
}

class RM<bits<2> major, dag outs, dag ins, string asm,
         list<dag> pattern = []> : RISCCInst16<outs, ins, asm, pattern> {
  bits<3> rd;
  bits<3> base;
  bits<8> disp;
  let Inst{15-14} = major;
  let Inst{13-11} = rd;
  let Inst{10-8} = base;
  let Inst{7-0} = disp;
}

class CompactRM<bits<1> store, dag outs, dag ins, string asm,
                list<dag> pattern = []> : RISCCInst16<outs, ins, asm, pattern> {
  bits<3> rd;
  bits<3> base;
  bits<8> disp;
  let Inst{15-14} = 0b01;
  let Inst{13-11} = rd;
  let Inst{10-8} = base;
  let Inst{7-1} = disp{7-1};
  let Inst{0} = store;
}

class RB<bits<3> cc, dag outs, dag ins, string asm,
         list<dag> pattern = []> : RISCCInst16<outs, ins, asm, pattern> {
  bits<8> target;
  let Inst{15-14} = 0b10;
  let Inst{13-11} = cc;
  let Inst{10-8} = 0b111;
  let Inst{7-0} = target;
}

class Pseudo<dag outs, dag ins, list<dag> pattern = []>
  : RISCCInst<outs, ins, "", pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let Size = 0;
}

class MCPseudo<dag outs, dag ins, string asm, int size>
  : RISCCInst<outs, ins, asm> {
  let isPseudo = 1;
  let Size = size;
}
