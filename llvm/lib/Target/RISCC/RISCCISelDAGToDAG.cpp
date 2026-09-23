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
#include "llvm/IR/Constants.h"
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
  bool selectMaskedMerge(SDNode *);

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
        C && Subtarget->isLegalWordOffset(C->getSExtValue())) {
      Base = Ptr.getOperand(0);
      Displacement = C->getSExtValue();
    } else {
      // Also fold base + (index + displacement). Shared index calculations
      // often keep the displacement inside the second add.
      for (unsigned Side = 0; Side != 2; ++Side) {
        SDValue Sum = Ptr.getOperand(Side);
        if (Sum.getOpcode() != ISD::ADD)
          continue;
        auto *C = dyn_cast<ConstantSDNode>(Sum.getOperand(1));
        if (!C || !Subtarget->isLegalWordOffset(C->getSExtValue()))
          continue;
        SDValue A = Ptr.getOperand(1 - Side), B = Sum.getOperand(0);
        SDNode *Existing = CurDAG->getNodeIfExists(
            ISD::ADD, CurDAG->getVTList(XLenVT), {A, B});
        if (!Existing && !Sum.hasOneUse())
          continue;
        // New nodes introduced during selection must already be selected.
        Base = Existing ? SDValue(Existing, 0)
                        : SDValue(CurDAG->getMachineNode(Subtarget->isRC32()
                                                             ? RISCC::ADD32
                                                             : RISCC::ADD,
                                                         DL, XLenVT, A, B),
                                  0);
        Displacement = C->getSExtValue();
        break;
      }
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

bool RISCCDAGToDAGISel::selectMaskedMerge(SDNode *N) {
  MVT VT = Subtarget->getXLenVT();
  if (N->getValueType(0) != VT)
    return false;
  SDValue A = N->getOperand(0), B = N->getOperand(1);
  if (A.getOpcode() != ISD::AND || B.getOpcode() != ISD::AND ||
      !A.hasOneUse() || !B.hasOneUse())
    return false;
  auto *AMask = dyn_cast<ConstantSDNode>(A.getOperand(1));
  auto *BMask = dyn_cast<ConstantSDNode>(B.getOperand(1));
  if (!AMask || !BMask || AMask->getAPIntValue() != ~BMask->getAPIntValue())
    return false;

  // (a & mask) | (b & ~mask) = b ^ ((a ^ b) & mask).
  // Keep only the cheaper mask: ANDI needs no constant register, and RC16
  // materializes a high byte with one LUI instead of an LUI/ORI pair.
  auto MaskCost = [&](uint64_t Mask) {
    if (isUInt<8>(Mask))
      return 0;
    return Subtarget->isRC32() || !(Mask & 255) ? 1 : 2;
  };
  if (MaskCost(BMask->getZExtValue()) < MaskCost(AMask->getZExtValue()))
    std::swap(A, B);
  SDValue Mask = A.getOperand(1);
  uint64_t MaskValue = cast<ConstantSDNode>(Mask)->getZExtValue();
  SDLoc DL(N);
  unsigned Xor = Subtarget->isRC32() ? RISCC::XOR32 : RISCC::XOR;
  SDValue Diff(
      CurDAG->getMachineNode(Xor, DL, VT, A.getOperand(0), B.getOperand(0)), 0);
  unsigned And = Subtarget->isRC32() ? RISCC::AND32 : RISCC::AND;
  if (isUInt<8>(MaskValue)) {
    And = Subtarget->isRC32() ? RISCC::ANDI32 : RISCC::ANDI;
    Mask = CurDAG->getTargetConstant(MaskValue, DL, VT);
  }
  SDValue Selected(CurDAG->getMachineNode(And, DL, VT, Diff, Mask), 0);
  CurDAG->SelectNodeTo(N, Xor, VT, B.getOperand(0), Selected);
  return true;
}

void RISCCDAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }
  SDLoc DL(N);
  switch (N->getOpcode()) {
  case ISD::OR:
    if (selectMaskedMerge(N))
      return;
    break;
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
    if (Subtarget->isRC32()) {
      // Keep constants visible until selection so immediates and address
      // displacements fold normally. Only materialized values need a pool.
      Constant *C =
          ConstantInt::get(Type::getInt32Ty(*CurDAG->getContext()), Value);
      SDValue Pool = CurDAG->getTargetConstantPool(C, MVT::i32, Align(4));
      auto *Load = CurDAG->getMachineNode(RISCC::LDPC, DL, MVT::i32, MVT::Other,
                                          Pool, CurDAG->getEntryNode());
      MachineFunction &MF = CurDAG->getMachineFunction();
      auto *MMO =
          MF.getMachineMemOperand(MachinePointerInfo::getConstantPool(MF),
                                  MachineMemOperand::MOLoad, 4, Align(4));
      CurDAG->setNodeMemRefs(Load, {MMO});
      ReplaceNode(N, Load);
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
    MachineMemOperand *MMO = LD->getMemOperand();
    auto SelectLoad = [&](unsigned Opcode, ArrayRef<SDValue> Ops) {
      auto *Load = CurDAG->SelectNodeTo(N, Opcode, Subtarget->getXLenVT(),
                                        MVT::Other, Ops);
      CurDAG->setNodeMemRefs(cast<MachineSDNode>(Load), {MMO});
    };
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
      SelectLoad(RISCC::LDPC, {TargetCP, Chain});
      return;
    }
    if (LD->getMemoryVT() == Subtarget->getXLenVT()) {
      auto [Base, Disp] = selectWordAddress(Ptr, DL);
      // Keep a shared address in a register when stores or other accesses
      // also need it; LDX would otherwise keep both inputs live as well.
      if (Base == Ptr && Ptr.getOpcode() == ISD::ADD && Ptr.hasOneUse())
        SelectLoad(Subtarget->isRC32() ? RISCC::LDX32 : RISCC::LDX,
                   {Ptr.getOperand(0), Ptr.getOperand(1), Chain});
      else
        SelectLoad(Subtarget->isRC32()   ? RISCC::LD32
                   : Subtarget->isNano() ? RISCC::LD_NANO
                                         : RISCC::LD,
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
      SelectLoad(Opc, {Ptr, Chain});
      return;
    }
    if (Subtarget->isRC32() && LD->getMemoryVT() == MVT::i16) {
      SelectLoad(LD->getExtensionType() == ISD::SEXTLOAD ? RISCC::LDHS
                                                         : RISCC::LDH,
                 {Ptr, Chain});
      return;
    }
    break;
  }
  case ISD::STORE: {
    auto *ST = cast<StoreSDNode>(N);
    SDValue Chain = ST->getChain(), Val = ST->getValue(),
            Ptr = ST->getBasePtr();
    MachineMemOperand *MMO = ST->getMemOperand();
    auto SelectStore = [&](unsigned Opcode, ArrayRef<SDValue> Ops) {
      auto *Store = CurDAG->SelectNodeTo(N, Opcode, MVT::Other, Ops);
      CurDAG->setNodeMemRefs(cast<MachineSDNode>(Store), {MMO});
    };
    if (ST->getMemoryVT() == Subtarget->getXLenVT()) {
      auto [Base, Disp] = selectWordAddress(Ptr, DL);
      SelectStore(Subtarget->isRC32()   ? RISCC::ST32
                  : Subtarget->isNano() ? RISCC::ST_NANO
                                        : RISCC::ST,
                  {Val, Base, Disp, Chain});
      return;
    }
    if (ST->getMemoryVT() == MVT::i8) {
      SelectStore(Subtarget->isRC32() ? RISCC::STB32 : RISCC::STB,
                  {Val, Ptr, Chain});
      return;
    }
    if (Subtarget->isRC32() && ST->getMemoryVT() == MVT::i16) {
      SelectStore(RISCC::STH, {Val, Ptr, Chain});
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
