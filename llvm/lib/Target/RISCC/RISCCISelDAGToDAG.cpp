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
        C && isInt<8>(C->getSExtValue())) {
      Base = Ptr.getOperand(0);
      Displacement = C->getSExtValue();
    }
  }
  SDValue Disp = CurDAG->getTargetConstant(
      APInt(16, Displacement, true), DL, MVT::i16);
  return {Base, Disp};
}

void RISCCDAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }
  SDLoc DL(N);
  switch (N->getOpcode()) {
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
      CurDAG->SelectNodeTo(N, RISCC::LDW, MVT::i16, MVT::Other,
                           {Base, Disp, Chain});
      return;
    }
    if (LD->getMemoryVT() == MVT::i8) {
      unsigned Opc = LD->getExtensionType() == ISD::SEXTLOAD ? RISCC::LDBS
                                                              : RISCC::LDB;
      SDNode *Zero = CurDAG->getMachineNode(
          RISCC::LDI, DL, MVT::i16,
          CurDAG->getTargetConstant(0, DL, MVT::i16));
      CurDAG->SelectNodeTo(N, Opc, MVT::i16, MVT::Other,
                           {Ptr, SDValue(Zero, 0), Chain});
      return;
    }
    break;
  }
  case ISD::STORE: {
    auto *ST = cast<StoreSDNode>(N);
    SDValue Chain = ST->getChain(), Val = ST->getValue(), Ptr = ST->getBasePtr();
    if (ST->getMemoryVT() == MVT::i16) {
      auto [Base, Disp] = selectWordAddress(Ptr, DL);
      CurDAG->SelectNodeTo(N, RISCC::STW, MVT::Other,
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
