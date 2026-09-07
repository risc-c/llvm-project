//===-- RISCCISelDAGToDAG.cpp - RISCC DAG Instruction Selector ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "RISCC.h"
#include "RISCCSubtarget.h"
#include "RISCCTargetMachine.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define DEBUG_TYPE "riscc-isel"

namespace {
class RISCCDAGToDAGISel final : public SelectionDAGISel {
  const RISCCSubtarget *Subtarget = nullptr;

public:
  explicit RISCCDAGToDAGISel(RISCCTargetMachine &TM, CodeGenOptLevel OL)
      : SelectionDAGISel(TM, OL) {}
  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<RISCCSubtarget>();
    return SelectionDAGISel::runOnMachineFunction(MF);
  }
  void Select(SDNode *) override;
  bool SelectInlineAsmMemoryOperand(const SDValue &, InlineAsm::ConstraintCode,
                                    std::vector<SDValue> &) override;
  std::pair<SDValue, SDValue> selectWordAddress(SDValue, const SDLoc &);

#include "RISCCGenDAGISel.inc"
};

class RISCCDAGToDAGISelLegacy final : public SelectionDAGISelLegacy {
public:
  static char ID;
  RISCCDAGToDAGISelLegacy(RISCCTargetMachine &TM, CodeGenOptLevel OL)
      : SelectionDAGISelLegacy(ID,
                               std::make_unique<RISCCDAGToDAGISel>(TM, OL)) {}
};
} // namespace

char RISCCDAGToDAGISelLegacy::ID = 0;

std::pair<SDValue, SDValue>
RISCCDAGToDAGISel::selectWordAddress(SDValue Ptr, const SDLoc &DL) {
  const MVT XLenVT = Subtarget->getXLenVT();
  SDValue Base = Ptr;
  int64_t Displacement = 0;
  if (Ptr.getOpcode() == ISD::ADD) {
    if (auto *C = dyn_cast<ConstantSDNode>(Ptr.getOperand(1));
        C && (Subtarget->isRC32()
                  ? isInt<9>(C->getSExtValue()) && !(C->getSExtValue() & 3)
                  : isInt<8>(C->getSExtValue()) && !(C->getSExtValue() & 1))) {
      Base = Ptr.getOperand(0);
      Displacement = C->getSExtValue();
    }
  }
  if (auto *FI = dyn_cast<FrameIndexSDNode>(Base))
    Base = CurDAG->getTargetFrameIndex(FI->getIndex(), XLenVT);
  SDValue Disp = CurDAG->getTargetConstant(
      APInt(XLenVT.getSizeInBits(), Displacement, true), DL, XLenVT);
  return {Base, Disp};
}

bool RISCCDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  if (ConstraintID != InlineAsm::ConstraintCode::m)
    return true;
  OutOps.push_back(Op);
  return false;
}

void RISCCDAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }
  SDLoc DL(N);
  switch (N->getOpcode()) {
  case RISCCISD::DIVU:
    CurDAG->SelectNodeTo(
        N, Subtarget->isRC32() ? RISCC::DIVU32 : RISCC::DIVU, N->getVTList(),
        {N->getOperand(0), N->getOperand(1), N->getOperand(2)});
    return;
  case RISCCISD::MULHU:
    CurDAG->SelectNodeTo(N, Subtarget->isRC32() ? RISCC::MULHU32 : RISCC::MULHU,
                         N->getVTList(), {N->getOperand(0), N->getOperand(1)});
    return;
  case ISD::SIGN_EXTEND_INREG:
    if (Subtarget->isRC32()) {
      MVT FromVT = cast<VTSDNode>(N->getOperand(1))->getVT().getSimpleVT();
      assert((FromVT == MVT::i8 || FromVT == MVT::i16) &&
             "unexpected RC32 sign extension width");
      CurDAG->SelectNodeTo(
          N, FromVT == MVT::i8 ? RISCC::SEXT8_RC32 : RISCC::SEXT16_RC32,
          MVT::i32, {N->getOperand(0)});
      return;
    }
    break;
  case ISD::Constant: {
    if (N->getValueType(0) != Subtarget->getXLenVT())
      break;
    uint64_t Value = cast<ConstantSDNode>(N)->getZExtValue();
    if (isUInt<8>(Value)) {
      CurDAG->SelectNodeTo(
          N, Subtarget->isRC32() ? RISCC::LDI32 : RISCC::LDI,
          Subtarget->getXLenVT(),
          CurDAG->getTargetConstant(Value, DL, Subtarget->getXLenVT()));
      return;
    }
    if (!Subtarget->isRC32() && (Value & 0xff) == 0) {
      CurDAG->SelectNodeTo(N, RISCC::LUI, MVT::i16,
                           CurDAG->getTargetConstant(Value >> 8, DL, MVT::i16));
      return;
    }
    break;
  }
  case ISD::FrameIndex: {
    const MVT XLenVT = Subtarget->getXLenVT();
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, XLenVT);
    SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i16);
    ReplaceNode(N,
                CurDAG->getMachineNode(Subtarget->isRC32() ? RISCC::FRAMEADDR32
                                                           : RISCC::FRAMEADDR,
                                       DL, XLenVT, TFI, Zero));
    return;
  }
  case ISD::LOAD: {
    auto *LD = cast<LoadSDNode>(N);
    SDValue Chain = LD->getChain(), Ptr = LD->getBasePtr();
    if (Subtarget->isRC32() && LD->getMemoryVT() == MVT::i32 &&
        isa<ConstantPoolSDNode>(Ptr)) {
      const auto *CP = cast<ConstantPoolSDNode>(Ptr);
      SDValue TargetCP;
      if (Ptr.getOpcode() == ISD::TargetConstantPool)
        TargetCP = Ptr;
      else if (CP->isMachineConstantPoolEntry())
        TargetCP = CurDAG->getTargetConstantPool(
            CP->getMachineCPVal(), MVT::i32, CP->getAlign(), CP->getOffset());
      else
        TargetCP = CurDAG->getTargetConstantPool(
            CP->getConstVal(), MVT::i32, CP->getAlign(), CP->getOffset());
      MachineMemOperand *MMO = LD->getMemOperand();
      auto *Load = cast<MachineSDNode>(CurDAG->SelectNodeTo(
          N, RISCC::LDPC, MVT::i32, MVT::Other, {TargetCP, Chain}));
      CurDAG->setNodeMemRefs(Load, {MMO});
      return;
    }
    if (LD->getMemoryVT() == Subtarget->getXLenVT()) {
      auto [Base, Disp] = selectWordAddress(Ptr, DL);
      if (Base == Ptr && Ptr.getOpcode() == ISD::ADD)
        CurDAG->SelectNodeTo(N, Subtarget->isRC32() ? RISCC::LDX32 : RISCC::LDX,
                             Subtarget->getXLenVT(), MVT::Other,
                             {Ptr.getOperand(0), Ptr.getOperand(1), Chain});
      else
        CurDAG->SelectNodeTo(N,
                             Subtarget->isRC32()   ? RISCC::LD32
                             : Subtarget->isNano() ? RISCC::LD_NANO
                                                   : RISCC::LD,
                             Subtarget->getXLenVT(), MVT::Other,
                             {Base, Disp, Chain});
      return;
    }
    if (LD->getMemoryVT() == MVT::i8) {
      const bool IsSigned = LD->getExtensionType() == ISD::SEXTLOAD;
      unsigned Opc;
      if (Subtarget->isRC32())
        Opc = IsSigned ? RISCC::LDBS32 : RISCC::LDB32;
      else
        Opc = IsSigned ? RISCC::LDBS : RISCC::LDB;
      CurDAG->SelectNodeTo(N, Opc, Subtarget->getXLenVT(), MVT::Other,
                           {Ptr, Chain});
      return;
    }
    if (Subtarget->isRC32() && LD->getMemoryVT() == MVT::i16) {
      CurDAG->SelectNodeTo(
          N, LD->getExtensionType() == ISD::SEXTLOAD ? RISCC::LDHS : RISCC::LDH,
          MVT::i32, MVT::Other, {Ptr, Chain});
      return;
    }
    break;
  }
  case ISD::STORE: {
    auto *ST = cast<StoreSDNode>(N);
    SDValue Chain = ST->getChain(), Val = ST->getValue(),
            Ptr = ST->getBasePtr();
    if (ST->getMemoryVT() == Subtarget->getXLenVT()) {
      auto [Base, Disp] = selectWordAddress(Ptr, DL);
      CurDAG->SelectNodeTo(N,
                           Subtarget->isRC32()   ? RISCC::ST32
                           : Subtarget->isNano() ? RISCC::ST_NANO
                                                 : RISCC::ST,
                           MVT::Other, {Val, Base, Disp, Chain});
      return;
    }
    if (ST->getMemoryVT() == MVT::i8) {
      CurDAG->SelectNodeTo(N, Subtarget->isRC32() ? RISCC::STB32 : RISCC::STB,
                           MVT::Other, {Val, Ptr, Chain});
      return;
    }
    if (Subtarget->isRC32() && ST->getMemoryVT() == MVT::i16) {
      CurDAG->SelectNodeTo(N, RISCC::STH, MVT::Other, {Val, Ptr, Chain});
      return;
    }
    break;
  }
  }
  SelectCode(N);
}

INITIALIZE_PASS(RISCCDAGToDAGISelLegacy, DEBUG_TYPE,
                "RISC-C DAG instruction selector", false, false)

FunctionPass *llvm::createRISCCISelDag(RISCCTargetMachine &TM,
                                       CodeGenOptLevel OL) {
  return new RISCCDAGToDAGISelLegacy(TM, OL);
}

RISCCISelDAGToDAGPass::RISCCISelDAGToDAGPass(RISCCTargetMachine &TM,
                                             CodeGenOptLevel OL)
    : SelectionDAGISelPass(std::make_unique<RISCCDAGToDAGISel>(TM, OL)) {}
