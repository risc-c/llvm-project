//===-- RISCCISelDAGToDAG.cpp - RISCC DAG Instruction Selector ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCC.h"
#include "RISCCSubtarget.h"
#include "RISCCTargetMachine.h"
#include "MCTargetDesc/RISCCMCTargetDesc.h"
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
  bool SelectInlineAsmMemoryOperand(
      const SDValue &, InlineAsm::ConstraintCode,
      std::vector<SDValue> &) override;
  std::pair<SDValue, SDValue> selectWordAddress(SDValue, const SDLoc &);

#include "RISCCGenDAGISel.inc"
};

class RISCCDAGToDAGISelLegacy final : public SelectionDAGISelLegacy {
public:
  static char ID;
  RISCCDAGToDAGISelLegacy(RISCCTargetMachine &TM, CodeGenOptLevel OL)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<RISCCDAGToDAGISel>(TM, OL)) {}
};
}

char RISCCDAGToDAGISelLegacy::ID = 0;

std::pair<SDValue, SDValue>
RISCCDAGToDAGISel::selectWordAddress(SDValue Ptr, const SDLoc &DL) {
  SDValue Base = Ptr;
  int64_t Displacement = 0;
  if (Ptr.getOpcode() == ISD::ADD) {
    if (auto *C = dyn_cast<ConstantSDNode>(Ptr.getOperand(1));
        C && isInt<8>(C->getSExtValue()) &&
        !(C->getSExtValue() & 1)) {
      Base = Ptr.getOperand(0);
      Displacement = C->getSExtValue();
    }
  }
  if (auto *FI = dyn_cast<FrameIndexSDNode>(Base))
    Base = CurDAG->getTargetFrameIndex(FI->getIndex(), MVT::i16);
  SDValue Disp = CurDAG->getTargetConstant(
      APInt(16, Displacement, true), DL, MVT::i16);
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
    CurDAG->SelectNodeTo(N, RISCC::DIVU, N->getVTList(),
                         {N->getOperand(0), N->getOperand(1),
                          N->getOperand(2)});
    return;
  case RISCCISD::MULHU:
    CurDAG->SelectNodeTo(N, RISCC::MULHU, N->getVTList(),
                         {N->getOperand(0), N->getOperand(1)});
    return;
  case ISD::Constant: {
    if (N->getValueType(0) != MVT::i16)
      break;
    uint64_t Value = cast<ConstantSDNode>(N)->getZExtValue();
    if (isUInt<8>(Value)) {
      CurDAG->SelectNodeTo(
          N, RISCC::LDI, MVT::i16,
          CurDAG->getTargetConstant(Value, DL, MVT::i16));
      return;
    }
    if ((Value & 0xff) == 0) {
      CurDAG->SelectNodeTo(
          N, RISCC::LUI, MVT::i16,
          CurDAG->getTargetConstant(Value >> 8, DL, MVT::i16));
      return;
    }
    break;
  }
  case ISD::FrameIndex: {
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, MVT::i16);
    SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i16);
    ReplaceNode(N, CurDAG->getMachineNode(RISCC::FRAMEADDR, DL, MVT::i16,
                                         TFI, Zero));
    return;
  }
  case ISD::LOAD: {
    auto *LD = cast<LoadSDNode>(N);
    SDValue Chain = LD->getChain(), Ptr = LD->getBasePtr();
    if (LD->getMemoryVT() == MVT::i16) {
      auto [Base, Disp] = selectWordAddress(Ptr, DL);
      if (Base == Ptr && Ptr.getOpcode() == ISD::ADD)
        CurDAG->SelectNodeTo(N, RISCC::LDX, MVT::i16, MVT::Other,
                             {Ptr.getOperand(0), Ptr.getOperand(1), Chain});
      else
        CurDAG->SelectNodeTo(N,
                             Subtarget->isNano() ? RISCC::LD_NANO : RISCC::LD,
                             MVT::i16, MVT::Other,
                             {Base, Disp, Chain});
      return;
    }
    if (LD->getMemoryVT() == MVT::i8) {
      unsigned Opc = LD->getExtensionType() == ISD::SEXTLOAD ? RISCC::LDBS
                                                              : RISCC::LDB;
      CurDAG->SelectNodeTo(N, Opc, MVT::i16, MVT::Other, {Ptr, Chain});
      return;
    }
    break;
  }
  case ISD::STORE: {
    auto *ST = cast<StoreSDNode>(N);
    SDValue Chain = ST->getChain(), Val = ST->getValue(), Ptr = ST->getBasePtr();
    if (ST->getMemoryVT() == MVT::i16) {
      auto [Base, Disp] = selectWordAddress(Ptr, DL);
      CurDAG->SelectNodeTo(N,
                           Subtarget->isNano() ? RISCC::ST_NANO : RISCC::ST,
                           MVT::Other,
                           {Val, Base, Disp, Chain});
      return;
    }
    if (ST->getMemoryVT() == MVT::i8) {
      CurDAG->SelectNodeTo(N, RISCC::STB, MVT::Other, {Val, Ptr, Chain});
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
