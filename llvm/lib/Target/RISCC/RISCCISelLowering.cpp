//===-- RISCCISelLowering.cpp - RISCC DAG Lowering ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCISelLowering.h"
#include "RISCCConstantPoolValue.h"
#include "RISCCInstrInfo.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSubtarget.h"
#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include <climits>

using namespace llvm;

static SDValue loadRC32Literal(SDValue Address, const SDLoc &DL,
                               SelectionDAG &DAG) {
  return DAG.getLoad(MVT::i32, DL, DAG.getEntryNode(), Address,
                     MachinePointerInfo::getConstantPool(
                         DAG.getMachineFunction()));
}

#define GET_CALLING_CONV_IMPL
#include "RISCCGenCallingConv.inc"

//===----------------------------------------------------------------------===//
// Target operation legalization
//===----------------------------------------------------------------------===//

RISCCTargetLowering::RISCCTargetLowering(const TargetMachine &TM,
                                         const RISCCSubtarget &STI)
    : TargetLowering(TM, STI), STI(STI) {
  if (STI.isRC32()) {
    addRegisterClass(MVT::i32, STI.getGPRClass());
    computeRegisterProperties(STI.getRegisterInfo());
    setStackPointerRegisterToSaveRestore(RISCC::R7);
    setBooleanContents(ZeroOrOneBooleanContent);
    setMinFunctionAlignment(Align(2));
    setPrefFunctionAlignment(Align(2));
    setMaxAtomicSizeInBitsSupported(0);
    setMinimumJumpTableEntries(UINT_MAX);

    for (unsigned Op : {ISD::ADD, ISD::SUB, ISD::AND, ISD::OR, ISD::XOR})
      setOperationAction(Op, MVT::i32, Legal);
    setOperationAction(ISD::MUL, MVT::i32, LibCall);
    for (unsigned Op : {ISD::SHL, ISD::SRL, ISD::SRA})
      setOperationAction(Op, MVT::i32, Custom);
    for (unsigned Op : {ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM})
      setOperationAction(Op, MVT::i32, LibCall);
    setLoadExtAction(ISD::EXTLOAD, MVT::i32, MVT::i8, Legal);
    setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i8, Legal);
    setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i8, Legal);
    setLoadExtAction(ISD::EXTLOAD, MVT::i32, MVT::i16, Legal);
    setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i16, Legal);
    setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i16, Legal);
    setTruncStoreAction(MVT::i32, MVT::i8, Legal);
    setTruncStoreAction(MVT::i32, MVT::i16, Legal);
    setOperationAction(ISD::FrameIndex, MVT::i32, Legal);
    setOperationAction(ISD::Constant, MVT::i32, Custom);
    for (unsigned Op : {ISD::GlobalAddress, ISD::ExternalSymbol,
                        ISD::BlockAddress})
      setOperationAction(Op, MVT::i32, Custom);
    setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
    for (unsigned Op : {ISD::BR_CC, ISD::SETCC, ISD::SELECT_CC})
      setOperationAction(Op, MVT::i32, Custom);
    setOperationAction(ISD::SELECT, MVT::i32, Expand);
    setOperationAction(ISD::VASTART, MVT::Other, Custom);
    setOperationAction(ISD::VAEND, MVT::Other, Expand);
    setOperationAction(ISD::VAARG, MVT::Other, Expand);
    setOperationAction(ISD::VACOPY, MVT::Other, Expand);
    return;
  }
  addRegisterClass(MVT::i16, &RISCC::GPRRegClass);
  computeRegisterProperties(STI.getRegisterInfo());
  setStackPointerRegisterToSaveRestore(RISCC::R7);
  setBooleanContents(ZeroOrOneBooleanContent);
  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));
  setMaxAtomicSizeInBitsSupported(0);
  setMinimumJumpTableEntries(UINT_MAX);

  for (unsigned Op : {ISD::ADD, ISD::SUB, ISD::AND, ISD::OR, ISD::XOR})
    setOperationAction(Op, MVT::i16, Legal);
  setOperationAction(ISD::MUL, MVT::i16,
                     STI.hasMul() ? Legal : LibCall);
  for (unsigned Op : {ISD::SHL, ISD::SRL, ISD::SRA})
    setOperationAction(Op, MVT::i16, Custom);
  // Custom is treated as available by integer type legalization, allowing
  // wide shifts to form i16 funnels which are then lowered here.
  for (unsigned Op : {ISD::FSHL, ISD::FSHR})
    setOperationAction(Op, MVT::i16, STI.isNano() ? Expand : Custom);
  // These multi-result nodes have no native instruction.  Marking them
  // Expand (rather than leaving the default Legal action) makes illegal wide
  // variable shifts use the mapped __*si3/__*di3 runtime helpers.
  for (unsigned Op : {ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS})
    setOperationAction(Op, MVT::i16, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i16,
                     STI.hasMul() ? Custom : Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i16,
                     STI.hasMul() ? Custom : Expand);
  setOperationAction(ISD::MULHU, MVT::i16,
                     STI.hasMulhu() ? Custom : Expand);
  setOperationAction(ISD::MULHS, MVT::i16, Expand);
  for (unsigned Op : {ISD::ROTL, ISD::ROTR})
    setOperationAction(Op, MVT::i16, Expand);
  for (unsigned Op : {ISD::BSWAP, ISD::CTLZ, ISD::CTTZ, ISD::CTPOP})
    setOperationAction(Op, MVT::i16, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16,
                     STI.isNano() ? Custom : Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
  for (unsigned Op : {ISD::SDIV, ISD::SREM})
    setOperationAction(Op, MVT::i16, LibCall);
  for (unsigned Op : {ISD::UDIV, ISD::UREM})
    setOperationAction(Op, MVT::i16,
                       STI.hasDivu() ? Custom : LibCall);
  for (MVT VT : {MVT::i16, MVT::i64}) {
    setOperationAction(ISD::SDIVREM, VT, Expand);
    setOperationAction(ISD::UDIVREM, VT,
                       VT == MVT::i16 && STI.hasDivu() ? Custom : Expand);
  }
  // Keep an illegal i32 div/rem pair together so the DAG combiner can select
  // the ABI's single __{u}divmodsi4 call instead of dividing and reconstructing
  // the remainder with quotient * denominator.
  setOperationAction(ISD::SDIVREM, MVT::i32, Custom);
  setOperationAction(ISD::UDIVREM, MVT::i32, Custom);
  // Wide multiplication is size-first: expanding an i32 multiply around the
  // call site duplicates a sizeable six-MUL sequence, while __mulsi3 is a
  // compact shared helper.  Keep i16 MUL native above.
  setOperationAction(ISD::MUL, MVT::i32, Custom);
  for (MVT VT : {MVT::i64}) {
    for (unsigned Op : {ISD::MUL, ISD::SHL, ISD::SRL, ISD::SRA,
                        ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM})
      setOperationAction(Op, VT, LibCall);
  }
  for (unsigned Op : {ISD::SHL, ISD::SRL, ISD::SRA, ISD::SDIV, ISD::UDIV,
                      ISD::SREM, ISD::UREM})
    setOperationAction(Op, MVT::i32, LibCall);

  setOperationAction(ISD::GlobalAddress, MVT::i16, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i16, Custom);
  setOperationAction(ISD::ExternalSymbol, MVT::i16, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i16, Custom);
  setOperationAction(ISD::ADDRSPACECAST, MVT::i16, Custom);
  setOperationAction(ISD::BR_CC, MVT::i16, Custom);
  setOperationAction(ISD::SETCC, MVT::i16, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i16, Custom);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::SELECT, MVT::i16, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::JumpTable, MVT::i16, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i16, Custom);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);

  setLoadExtAction(ISD::EXTLOAD, MVT::i16, MVT::i8, Legal);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i16, MVT::i8, Legal);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i16, MVT::i8,
                   STI.isNano() ? Expand : Legal);
  setTruncStoreAction(MVT::i16, MVT::i8, Legal);

  setTargetDAGCombine(ISD::MUL);
  setTargetDAGCombine(ISD::OR);
  setTargetDAGCombine(ISD::SELECT);
  setTargetDAGCombine(ISD::SUB);
}

MVT RISCCTargetLowering::getScalarShiftAmountTy(const DataLayout &, EVT) const {
  return STI.getXLenVT();
}

MVT::SimpleValueType RISCCTargetLowering::getCmpLibcallReturnType() const {
  return STI.getXLenVT().SimpleTy;
}

//===----------------------------------------------------------------------===//
// Custom DAG lowering
//===----------------------------------------------------------------------===//

static SDValue expandSmallConstantMultiply(SDValue Value, int64_t Multiplier,
                                           SelectionDAG &DAG,
                                           const SDLoc &DL) {
  bool Negate = Multiplier < 0;
  uint64_t Magnitude = Negate ? -Multiplier : Multiplier;
  unsigned Cost = Log2_64(Magnitude) + popcount(Magnitude) - 1 + Negate;
  if (Cost > 4)
    return {};

  SDValue Product = Value;
  uint64_t Bit = (uint64_t(1) << Log2_64(Magnitude)) >> 1;
  for (; Bit; Bit >>= 1) {
    Product = DAG.getNode(ISD::ADD, DL, MVT::i16, Product, Product);
    if (Magnitude & Bit)
      Product = DAG.getNode(ISD::ADD, DL, MVT::i16, Product, Value);
  }
  if (Negate)
    Product =
        DAG.getNode(ISD::SUB, DL, MVT::i16,
                    DAG.getConstant(0, DL, MVT::i16), Product);
  return Product;
}

static bool matchShiftChain(SDValue Value, unsigned GenericOpcode,
                            unsigned TargetOpcode, unsigned Amount,
                            SDValue &Source) {
  while (Amount) {
    unsigned Opcode = Value.getOpcode();
    if (Opcode == GenericOpcode || Opcode == TargetOpcode) {
      const auto *C = dyn_cast<ConstantSDNode>(Value.getOperand(1));
      if (!C || C->getZExtValue() == 0 ||
          C->getZExtValue() > Amount)
        return false;
      Amount -= C->getZExtValue();
      Value = Value.getOperand(0);
      continue;
    }

    // Min lowers a one-bit left shift to add(x, x).
    if (GenericOpcode == ISD::SHL && Opcode == ISD::ADD &&
        Value.getOperand(0) == Value.getOperand(1)) {
      --Amount;
      Value = Value.getOperand(0);
      continue;
    }
    return false;
  }
  Source = Value;
  return true;
}

SDValue
RISCCTargetLowering::PerformDAGCombine(SDNode *N,
                                       DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  if (N->getOpcode() == ISD::OR && !STI.isNano() &&
      N->getValueType(0) == MVT::i16) {
    SDValue A = N->getOperand(0);
    SDValue B = N->getOperand(1);
    SDValue High;
    SDValue Low;
    if ((matchShiftChain(A, ISD::SHL, RISCCISD::SHL, 1, High) &&
         matchShiftChain(B, ISD::SRL, RISCCISD::SRL, 15, Low)) ||
        (matchShiftChain(B, ISD::SHL, RISCCISD::SHL, 1, High) &&
         matchShiftChain(A, ISD::SRL, RISCCISD::SRL, 15, Low)))
      return DAG.getNode(RISCCISD::FSL1, DL, MVT::i16, High, Low);

    if ((matchShiftChain(A, ISD::SRL, RISCCISD::SRL, 1, Low) &&
         matchShiftChain(B, ISD::SHL, RISCCISD::SHL, 15, High)) ||
        (matchShiftChain(B, ISD::SRL, RISCCISD::SRL, 1, Low) &&
         matchShiftChain(A, ISD::SHL, RISCCISD::SHL, 15, High)))
      return DAG.getNode(RISCCISD::FSR1, DL, MVT::i16, Low, High);
  }

  if (N->getOpcode() == ISD::MUL && !STI.hasMul() &&
      N->getValueType(0) == MVT::i16) {
    SDValue Value = N->getOperand(0);
    auto *Multiplier = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (!Multiplier) {
      Value = N->getOperand(1);
      Multiplier = dyn_cast<ConstantSDNode>(N->getOperand(0));
    }
    if (Multiplier) {
      int64_t Amount = Multiplier->getSExtValue();
      if (Amount > 1 || Amount < -1)
        return expandSmallConstantMultiply(Value, Amount, DAG, DL);
    }
    return {};
  }

  if (N->getOpcode() == ISD::SELECT &&
      N->getValueType(0) == MVT::i16) {
    SDValue Cond = N->getOperand(0);
    SDValue TrueValue = N->getOperand(1);
    SDValue FalseValue = N->getOperand(2);
    bool TrueIsZero = isNullConstant(TrueValue);
    bool FalseIsZero = isNullConstant(FalseValue);
    if (TrueIsZero == FalseIsZero)
      return {};

    SDValue Bool = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i16, Cond);
    SDValue Mask;
    SDValue Value;
    if (FalseIsZero) {
      Mask = DAG.getNode(ISD::SUB, DL, MVT::i16,
                         DAG.getConstant(0, DL, MVT::i16), Bool);
      Value = TrueValue;
    } else {
      Mask = DAG.getNode(ISD::ADD, DL, MVT::i16, Bool,
                         DAG.getSignedConstant(-1, DL, MVT::i16));
      Value = FalseValue;
    }
    return DAG.getNode(ISD::AND, DL, MVT::i16, Value, Mask);
  }

  /*
   * InstCombine canonicalizes a paired div/rem to
   *   quotient = x / y; remainder = x - quotient * y
   * before target lowering.  Re-form the pair because one restoring divide is
   * far cheaper than a divide followed by a multiply and subtraction.
   */
  if (N->getOpcode() == ISD::SUB && N->getValueType(0).isSimple() &&
      (N->getValueType(0).getSimpleVT() == MVT::i32 ||
       (N->getValueType(0).getSimpleVT() == MVT::i16 && STI.hasDivu()))) {
    MVT VT = N->getValueType(0).getSimpleVT();
    SDValue Dividend = N->getOperand(0);
    SDValue Product = N->getOperand(1);
    if (Product.getOpcode() == ISD::MUL) {
      SDValue Div = Product.getOperand(0);
      SDValue Divisor = Product.getOperand(1);
      if (Div.getOpcode() != ISD::SDIV && Div.getOpcode() != ISD::UDIV) {
        std::swap(Div, Divisor);
      }
      if ((Div.getOpcode() == ISD::SDIV ||
           Div.getOpcode() == ISD::UDIV) &&
          Div.getOperand(0) == Dividend &&
          Div.getOperand(1) == Divisor) {
        // The MDU has only unsigned division. Keep signed i16 division on
        // the normal helper path; i32 retains its existing paired libcall.
        if (VT == MVT::i16 && Div.getOpcode() != ISD::UDIV)
          return {};
        unsigned Opcode = Div.getOpcode() == ISD::SDIV
                              ? ISD::SDIVREM
                              : ISD::UDIVREM;
        SDValue DivRem = DAG.getNode(
            Opcode, DL, DAG.getVTList(VT, VT), Dividend, Divisor);
        DCI.CombineTo(Div.getNode(), DivRem.getValue(0));
        return DivRem.getValue(1);
      }
    }
  }

  return {};
}

SDValue RISCCTargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return lowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress:
    return lowerGlobalTLSAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return lowerExternalSymbol(Op, DAG);
  case ISD::BlockAddress:
    return lowerBlockAddress(Op, DAG);
  case ISD::ADDRSPACECAST:
    return lowerAddrSpaceCast(Op, DAG);
  case ISD::Constant:
    return lowerConstant(Op, DAG);
  case ISD::BR_CC:
    return lowerBRCC(Op, DAG);
  case ISD::SETCC:
    return lowerSETCC(Op, DAG);
  case ISD::SELECT_CC:
    return lowerSELECTCC(Op, DAG);
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
    return lowerShift(Op, DAG);
  case ISD::FSHL:
  case ISD::FSHR:
    return lowerFunnelShift(Op, DAG);
  case ISD::SIGN_EXTEND_INREG: {
    assert(STI.isNano() &&
           cast<VTSDNode>(Op.getOperand(1))->getVT() == MVT::i8);
    SDLoc DL(Op);
    SDValue Value = DAG.getNode(
        ISD::AND, DL, MVT::i16, Op.getOperand(0),
        DAG.getConstant(0xff, DL, MVT::i16));
    Value = DAG.getNode(ISD::XOR, DL, MVT::i16, Value,
                        DAG.getConstant(0x80, DL, MVT::i16));
    return DAG.getNode(ISD::ADD, DL, MVT::i16, Value,
                       DAG.getConstant(-128, DL, MVT::i16));
  }
  case ISD::UMUL_LOHI:
    return lowerMULLOHI(Op, DAG, false);
  case ISD::SMUL_LOHI:
    return lowerMULLOHI(Op, DAG, true);
  case ISD::MULHU: {
    SDLoc DL(Op);
    SDValue Product = DAG.getNode(ISD::UMUL_LOHI, DL,
                                  DAG.getVTList(MVT::i16, MVT::i16),
                                  Op.getOperand(0), Op.getOperand(1));
    return lowerMULLOHI(Product, DAG, false).getValue(1);
  }
  case ISD::MUL:
    return lowerMul(Op, DAG);
  case ISD::UDIV:
  case ISD::UREM:
    return lowerUDivRem(Op, DAG);
  case ISD::UDIVREM:
    if (Op.getValueType() == MVT::i16)
      return lowerUDivRem(Op, DAG);
    return lowerDivRem(Op, DAG);
  case ISD::SDIVREM:
    return lowerDivRem(Op, DAG);
  case ISD::VASTART:
    return lowerVASTART(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC: {
    const Function &Fn = DAG.getMachineFunction().getFunction();
    DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
        Fn, "RISC-C does not support dynamic stack allocation",
        SDLoc(Op).getDebugLoc()));
    SDValue Results[] = {
        DAG.getConstant(0, SDLoc(Op), Op.getValueType()), Op.getOperand(0)};
    return DAG.getMergeValues(Results, SDLoc(Op));
  }
  default:
    llvm_unreachable("unexpected custom RISC-C lowering");
  }
}

void RISCCTargetLowering::ReplaceNodeResults(
    SDNode *N, SmallVectorImpl<SDValue> &Results,
    SelectionDAG &DAG) const {
  SDValue Res = LowerOperation(SDValue(N, 0), DAG);
  if (!Res)
    return;
  for (unsigned I = 0, E = Res->getNumValues(); I != E; ++I)
    Results.push_back(Res.getValue(I));
}

SDValue RISCCTargetLowering::lowerFunnelShift(SDValue Op,
                                              SelectionDAG &DAG) const {
  bool IsLeft = Op.getOpcode() == ISD::FSHL;
  SDValue High = Op.getOperand(0);
  SDValue Low = Op.getOperand(1);
  SDValue Amount = Op.getOperand(2);
  SDLoc DL(Op);
  if (Amount.getValueType() != MVT::i16)
    Amount = DAG.getZExtOrTrunc(Amount, DL, MVT::i16);

  auto Shift = [&](unsigned Opcode, SDValue Value, SDValue Count) {
    return lowerShift(DAG.getNode(Opcode, DL, MVT::i16, Value, Count), DAG);
  };

  if (const auto *C = dyn_cast<ConstantSDNode>(Amount)) {
    unsigned Count = C->getZExtValue() & 15;
    if (Count == 0)
      return IsLeft ? High : Low;
    if (Count == 1)
      return DAG.getNode(IsLeft ? RISCCISD::FSL1 : RISCCISD::FSR1, DL,
                         MVT::i16, IsLeft ? High : Low,
                         IsLeft ? Low : High);
    if (Count == 15)
      return DAG.getNode(IsLeft ? RISCCISD::FSR1 : RISCCISD::FSL1, DL,
                         MVT::i16, IsLeft ? Low : High,
                         IsLeft ? High : Low);

    SDValue CountValue = DAG.getConstant(Count, DL, MVT::i16);
    SDValue Inverse = DAG.getConstant(16 - Count, DL, MVT::i16);
    SDValue ShiftedHigh =
        Shift(ISD::SHL, High, IsLeft ? CountValue : Inverse);
    SDValue ShiftedLow =
        Shift(ISD::SRL, Low, IsLeft ? Inverse : CountValue);
    return DAG.getNode(ISD::OR, DL, MVT::i16, ShiftedHigh, ShiftedLow);
  }

  // Masking makes both variable counts modulo 16.  The two-step side of
  // each expansion avoids ever expressing a shift by 16.
  SDValue Mask = DAG.getConstant(15, DL, MVT::i16);
  Amount = DAG.getNode(ISD::AND, DL, MVT::i16, Amount, Mask);
  SDValue Inverse = DAG.getNode(
      ISD::AND, DL, MVT::i16, DAG.getNOT(DL, Amount, MVT::i16), Mask);
  SDValue One = DAG.getConstant(1, DL, MVT::i16);
  SDValue ShiftedHigh;
  SDValue ShiftedLow;
  if (IsLeft) {
    ShiftedHigh = Shift(ISD::SHL, High, Amount);
    ShiftedLow = Shift(ISD::SRL, Shift(ISD::SRL, Low, One), Inverse);
  } else {
    ShiftedHigh = Shift(ISD::SHL, Shift(ISD::SHL, High, One), Inverse);
    ShiftedLow = Shift(ISD::SRL, Low, Amount);
  }
  return DAG.getNode(ISD::OR, DL, MVT::i16, ShiftedHigh, ShiftedLow);
}

SDValue RISCCTargetLowering::lowerVASTART(SDValue Op,
                                          SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  const auto *FuncInfo = MF.getInfo<RISCCMachineFunctionInfo>();
  SDLoc DL(Op);
  SDValue FirstVarArg = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(),
                                          getPointerTy(MF.getDataLayout()));
  const Value *SrcValue = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, FirstVarArg, Op.getOperand(1),
                      MachinePointerInfo(SrcValue));
}

SDValue RISCCTargetLowering::lowerMULLOHI(SDValue Op, SelectionDAG &DAG,
                                           bool Signed) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);
  if (!Signed && STI.hasMulhu()) {
    SDValue Product = DAG.getNode(RISCCISD::MULHU, DL,
                                  DAG.getVTList(MVT::i16, MVT::i16), LHS,
                                  RHS);
    return DAG.getMergeValues({Product.getValue(0), Product.getValue(1)},
                              DL);
  }

  SDValue C8 = DAG.getConstant(8, DL, MVT::i16);
  SDValue C15 = DAG.getConstant(15, DL, MVT::i16);
  SDValue ByteMask = DAG.getConstant(0xff, DL, MVT::i16);

  // Compute the full unsigned product from four 8x8 products.  Every node is
  // i16: introducing an illegal i32 from custom operation legalization would
  // violate LegalizeDAG's phase ordering.  Byte operands ensure each native
  // low-half MUL contains the complete partial product.
  SDValue LL = DAG.getNode(ISD::AND, DL, MVT::i16, LHS, ByteMask);
  SDValue LH = DAG.getNode(ISD::SRL, DL, MVT::i16, LHS, C8);
  SDValue RL = DAG.getNode(ISD::AND, DL, MVT::i16, RHS, ByteMask);
  SDValue RH = DAG.getNode(ISD::SRL, DL, MVT::i16, RHS, C8);
  SDValue P0 = DAG.getNode(ISD::MUL, DL, MVT::i16, LL, RL);
  SDValue P1 = DAG.getNode(ISD::MUL, DL, MVT::i16, LL, RH);
  SDValue P2 = DAG.getNode(ISD::MUL, DL, MVT::i16, LH, RL);
  SDValue P3 = DAG.getNode(ISD::MUL, DL, MVT::i16, LH, RH);

  SDValue Mid = DAG.getNode(
      ISD::ADD, DL, MVT::i16,
      DAG.getNode(ISD::SRL, DL, MVT::i16, P0, C8),
      DAG.getNode(ISD::AND, DL, MVT::i16, P1, ByteMask));
  Mid = DAG.getNode(ISD::ADD, DL, MVT::i16, Mid,
                    DAG.getNode(ISD::AND, DL, MVT::i16, P2, ByteMask));
  SDValue Lo = DAG.getNode(
      ISD::OR, DL, MVT::i16,
      DAG.getNode(ISD::AND, DL, MVT::i16, P0, ByteMask),
      DAG.getNode(ISD::SHL, DL, MVT::i16,
                  DAG.getNode(ISD::AND, DL, MVT::i16, Mid, ByteMask), C8));

  SDValue Hi = DAG.getNode(ISD::ADD, DL, MVT::i16, P3,
                           DAG.getNode(ISD::SRL, DL, MVT::i16, P1, C8));
  Hi = DAG.getNode(ISD::ADD, DL, MVT::i16, Hi,
                   DAG.getNode(ISD::SRL, DL, MVT::i16, P2, C8));
  Hi = DAG.getNode(ISD::ADD, DL, MVT::i16, Hi,
                   DAG.getNode(ISD::SRL, DL, MVT::i16, Mid, C8));

  if (Signed) {
    // Convert unsigned high-half multiplication to signed high-half:
    // hi -= (lhs < 0 ? rhs : 0) + (rhs < 0 ? lhs : 0).
    SDValue LSign = DAG.getNode(ISD::SRA, DL, MVT::i16, LHS, C15);
    SDValue RSign = DAG.getNode(ISD::SRA, DL, MVT::i16, RHS, C15);
    Hi = DAG.getNode(
        ISD::SUB, DL, MVT::i16, Hi,
        DAG.getNode(ISD::AND, DL, MVT::i16, LSign, RHS));
    Hi = DAG.getNode(
        ISD::SUB, DL, MVT::i16, Hi,
        DAG.getNode(ISD::AND, DL, MVT::i16, RSign, LHS));
  }
  SDValue Results[] = {Lo, Hi};
  return DAG.getMergeValues(Results, DL);
}

SDValue RISCCTargetLowering::lowerMul(SDValue Op, SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::MUL && Op.getValueType() == MVT::i32);

  Type *I32 = Type::getInt32Ty(*DAG.getContext());
  ArgListTy Args;
  for (SDValue Value : Op->op_values())
    Args.emplace_back(Value, I32);

  SDLoc DL(Op);
  SDValue Callee =
      DAG.getExternalSymbol("__mulsi3", getPointerTy(DAG.getDataLayout()));
  CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, I32, Callee, std::move(Args));
  return LowerCallTo(CLI).first;
}

SDValue RISCCTargetLowering::lowerUDivRem(SDValue Op,
                                           SelectionDAG &DAG) const {
  assert(STI.hasDivu() && Op.getValueType() == MVT::i16 &&
         (Op.getOpcode() == ISD::UDIV || Op.getOpcode() == ISD::UREM ||
          Op.getOpcode() == ISD::UDIVREM));

  SDLoc DL(Op);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i16);
  SDValue Div = DAG.getNode(RISCCISD::DIVU, DL,
                            DAG.getVTList(MVT::i16, MVT::i16), Zero,
                            Op.getOperand(0), Op.getOperand(1));
  if (Op.getOpcode() == ISD::UDIV)
    return Div.getValue(1);
  if (Op.getOpcode() == ISD::UREM)
    return Div.getValue(0);
  return DAG.getMergeValues({Div.getValue(1), Div.getValue(0)}, DL);
}

SDValue RISCCTargetLowering::lowerShift(SDValue Op,
                                        SelectionDAG &DAG) const {
  if (STI.isRC32())
    return lowerRC32Shift(Op, DAG);

  unsigned TOpc = Op.getOpcode() == ISD::SHL ? RISCCISD::SHL
                  : Op.getOpcode() == ISD::SRL ? RISCCISD::SRL
                                               : RISCCISD::SRA;
  SDLoc DL(Op);
  if (const auto *C = dyn_cast<ConstantSDNode>(Op.getOperand(1))) {
    unsigned Amount = C->getZExtValue() & 15;
    SDValue V = Op.getOperand(0);
    const Function &Fn = DAG.getMachineFunction().getFunction();

    // A Min/Nano direct call is six bytes, but making a leaf function call
    // also costs return-address save/restore code.  Eleven is the first count
    // with a useful local size win after that overhead.  The archive supplies
    // one shared fall-through staircase per shift direction.
    if (!STI.hasWideShift() && Fn.hasMinSize() && Amount >= 11)
      return lowerShiftLibCall(V, Op.getOpcode(), Amount, DAG);

    while (Amount) {
      unsigned Chunk = STI.hasWideShift() ? std::min(Amount, 8u) : 1;
      if (!STI.hasWideShift() && Op.getOpcode() == ISD::SHL)
        V = DAG.getNode(ISD::ADD, DL, MVT::i16, V, V);
      else
        V = DAG.getNode(TOpc, DL, MVT::i16, V,
                        DAG.getConstant(Chunk, DL, MVT::i16));
      Amount -= Chunk;
    }
    return V;
  }
  return DAG.getNode(TOpc, DL, MVT::i16, Op.getOperand(0), Op.getOperand(1));
}

SDValue RISCCTargetLowering::lowerRC32Shift(SDValue Op,
                                            SelectionDAG &DAG) const {
  const char *Name = Op.getOpcode() == ISD::SHL   ? "__ashlsi3"
                     : Op.getOpcode() == ISD::SRL ? "__lshrsi3"
                                                   : "__ashrsi3";
  Type *I32 = Type::getInt32Ty(*DAG.getContext());
  ArgListTy Args;
  Args.emplace_back(Op.getOperand(0), I32);
  Args.emplace_back(Op.getOperand(1), I32);
  SDLoc DL(Op);
  SDValue Callee =
      DAG.getExternalSymbol(Name, getPointerTy(DAG.getDataLayout()));
  CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, I32, Callee, std::move(Args));
  return LowerCallTo(CLI).first;
}

SDValue RISCCTargetLowering::lowerShiftLibCall(
    SDValue Value, unsigned Opcode, unsigned Amount, SelectionDAG &DAG) const {
  const char *Stem = Opcode == ISD::SHL ? "__riscc_shlhi"
                     : Opcode == ISD::SRL ? "__riscc_lshrhi"
                                          : "__riscc_ashrhi";
  std::string Name = (Twine(Stem) + Twine(Amount)).str();
  SDLoc DL(Value);
  Type *I16 = Type::getInt16Ty(*DAG.getContext());
  ArgListTy Args;
  Args.emplace_back(Value, I16);

  const char *Symbol =
      DAG.getMachineFunction().createExternalSymbolName(Name);
  SDValue Callee = DAG.getTargetExternalSymbol(
      Symbol, getPointerTy(DAG.getDataLayout()),
      RISCCII::MO_SREG_PRESERVING_CALL);
  CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, I16, Callee, std::move(Args))
      .setIsPostTypeLegalization(true);
  return LowerCallTo(CLI).first;
}

SDValue RISCCTargetLowering::lowerDivRem(
    SDValue Op, SelectionDAG &DAG) const {
  bool IsSigned = Op.getOpcode() == ISD::SDIVREM;
  assert((IsSigned || Op.getOpcode() == ISD::UDIVREM) &&
         Op.getValueType() == MVT::i32);

  Type *I32 = Type::getInt32Ty(*DAG.getContext());
  ArgListTy Args;
  for (SDValue Value : Op->op_values()) {
    ArgListEntry Entry(Value, I32);
    Entry.IsSExt = IsSigned;
    Entry.IsZExt = !IsSigned;
    Args.push_back(Entry);
  }

  bool QuotientUnused = Op.getValue(0).use_empty();
  bool RemainderUnused = Op.getValue(1).use_empty();
  if (QuotientUnused || RemainderUnused) {
    const char *Name;
    if (QuotientUnused)
      Name = IsSigned ? "__modsi3" : "__umodsi3";
    else
      Name = IsSigned ? "__divsi3" : "__udivsi3";
    SDValue Callee =
        DAG.getExternalSymbol(Name, getPointerTy(DAG.getDataLayout()));
    SDLoc DL(Op);
    CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(DL)
        .setChain(DAG.getEntryNode())
        .setLibCallee(CallingConv::C, I32, Callee, std::move(Args))
        .setSExtResult(IsSigned)
        .setZExtResult(!IsSigned);
    SDValue Result = LowerCallTo(CLI).first;
    SDValue Unused = DAG.getPOISON(MVT::i32);
    return QuotientUnused
               ? DAG.getMergeValues({Unused, Result}, DL)
               : DAG.getMergeValues({Result, Unused}, DL);
  }

  SDValue RemPtr = DAG.CreateStackTemporary(MVT::i32);
  ArgListEntry RemArg(
      RemPtr, PointerType::getUnqual(*DAG.getContext()));
  Args.push_back(RemArg);

  SDValue Callee = DAG.getExternalSymbol(
      IsSigned ? "__divmodsi4" : "__udivmodsi4",
      getPointerTy(DAG.getDataLayout()));
  SDLoc DL(Op);
  CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, I32, Callee, std::move(Args))
      .setSExtResult(IsSigned)
      .setZExtResult(!IsSigned);
  std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);

  int FI = cast<FrameIndexSDNode>(RemPtr)->getIndex();
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI);
  SDValue Rem =
      DAG.getLoad(MVT::i32, DL, CallInfo.second, RemPtr, PtrInfo);
  return DAG.getMergeValues({CallInfo.first, Rem}, DL);
}

SDValue RISCCTargetLowering::lowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  auto *N = cast<GlobalAddressSDNode>(Op);
  if (STI.isRC32()) {
    LLVMContext &Context = *DAG.getContext();
    Type *WordTy = Type::getInt32Ty(Context);
    Constant *Address = ConstantExpr::getPtrToInt(
        const_cast<GlobalValue *>(N->getGlobal()), WordTy);
    if (N->getOffset())
      Address = ConstantExpr::getAdd(
          Address, ConstantInt::getSigned(WordTy, N->getOffset()));
    SDLoc DL(Op);
    SDValue Pool = DAG.getConstantPool(Address, MVT::i32, Align(4));
    return loadRC32Literal(Pool, DL, DAG);
  }
  SDValue T = DAG.getTargetGlobalAddress(N->getGlobal(), SDLoc(Op),
                                         Op.getValueType(), N->getOffset(),
                                         RISCCII::MO_None);
  return DAG.getNode(RISCCISD::Wrapper, SDLoc(Op), Op.getValueType(), T);
}

SDValue RISCCTargetLowering::lowerGlobalTLSAddress(SDValue Op,
                                                    SelectionDAG &DAG) const {
  const auto *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  SDLoc DL(Op);
  if (STI.isNano()) {
    DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
        DAG.getMachineFunction().getFunction(),
        "RISC-C Nano does not support thread-local storage", DL.getDebugLoc()));
    return DAG.getPOISON(Op.getValueType());
  }
  TLSModel::Model Model = getTargetMachine().getTLSModel(GV);
  // Clang leaves an external TLS declaration non-DSO-local even in a static
  // executable, so TargetMachine classifies it as InitialExec.  There is no
  // GOT or dynamic loader on RISC-C; both static classifications use this
  // one local-exec sequence and the final link verifies the TLS symbol.
  if (Model != TLSModel::LocalExec && Model != TLSModel::InitialExec) {
    DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
        DAG.getMachineFunction().getFunction(),
        "RISC-C supports only static local-exec TLS", DL.getDebugLoc()));
    return DAG.getPOISON(Op.getValueType());
  }
  if (STI.isRC32()) {
    SDValue Base = DAG.getCopyFromReg(DAG.getEntryNode(), DL, RISCC::S2,
                                      MVT::i32);
    auto *Symbol = RISCCConstantPoolSymbol::Create(*DAG.getContext(), GV,
                                                    /*IsTPOFF=*/true);
    SDValue Pool = DAG.getTargetConstantPool(Symbol, MVT::i32, Align(4));
    SDValue Offset = loadRC32Literal(Pool, DL, DAG);
    return DAG.getNode(ISD::ADD, DL, MVT::i32, Base, Offset);
  }

  // S2 is reserved from ordinary allocation and is the ABI thread pointer.
  // A physical copy is selected through RISCCInstrInfo::copyPhysReg as MFS.
  SDValue Base = DAG.getCopyFromReg(DAG.getEntryNode(), DL, RISCC::S2,
                                    MVT::i16);
  SDValue Offset = DAG.getTargetGlobalAddress(GV, DL, Op.getValueType(),
                                               N->getOffset(),
                                               RISCCII::MO_TPOFF);
  Offset = DAG.getNode(RISCCISD::Wrapper, DL, Op.getValueType(), Offset);
  return DAG.getNode(ISD::ADD, DL, Op.getValueType(), Base, Offset);
}

SDValue RISCCTargetLowering::lowerExternalSymbol(SDValue Op,
                                                 SelectionDAG &DAG) const {
  auto *N = cast<ExternalSymbolSDNode>(Op);
  if (STI.isRC32()) {
    SDLoc DL(Op);
    auto *Symbol =
        RISCCConstantPoolSymbol::Create(*DAG.getContext(), N->getSymbol());
    SDValue Pool = DAG.getTargetConstantPool(Symbol, MVT::i32, Align(4));
    return loadRC32Literal(Pool, DL, DAG);
  }
  SDValue T = DAG.getTargetExternalSymbol(N->getSymbol(), Op.getValueType(),
                                          RISCCII::MO_None);
  return DAG.getNode(RISCCISD::Wrapper, SDLoc(Op), Op.getValueType(), T);
}

SDValue RISCCTargetLowering::lowerBlockAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  auto *N = cast<BlockAddressSDNode>(Op);
  if (STI.isRC32()) {
    LLVMContext &Context = *DAG.getContext();
    Type *WordTy = Type::getInt32Ty(Context);
    Constant *Address = ConstantExpr::getPtrToInt(
        const_cast<BlockAddress *>(N->getBlockAddress()), WordTy);
    SDLoc DL(Op);
    SDValue Pool = DAG.getConstantPool(Address, MVT::i32, Align(4));
    return loadRC32Literal(Pool, DL, DAG);
  }
  SDValue T = DAG.getTargetBlockAddress(N->getBlockAddress(), Op.getValueType(),
                                        0, RISCCII::MO_None);
  return DAG.getNode(RISCCISD::Wrapper, SDLoc(Op), Op.getValueType(), T);
}

SDValue RISCCTargetLowering::lowerAddrSpaceCast(SDValue Op,
                                                SelectionDAG &DAG) const {
  return Op.getOperand(0);
}

SDValue RISCCTargetLowering::lowerConstant(SDValue Op,
                                           SelectionDAG &DAG) const {
  auto *N = cast<ConstantSDNode>(Op);
  if (isUInt<8>(N->getZExtValue()))
    return Op;
  Type *Ty = Type::getInt32Ty(*DAG.getContext());
  Constant *Value = ConstantInt::get(Ty, N->getAPIntValue());
  SDLoc DL(Op);
  SDValue Pool = DAG.getConstantPool(Value, MVT::i32, Align(4));
  return loadRC32Literal(Pool, DL, DAG);
}

SDValue RISCCTargetLowering::lowerBRCC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  if (isa<ConstantSDNode>(LHS) && !isa<ConstantSDNode>(RHS)) {
    std::swap(LHS, RHS);
    CC = ISD::getSetCCSwappedOperands(CC);
  }
  if (auto *C = dyn_cast<ConstantSDNode>(RHS)) {
    int64_t Immediate = C->getSExtValue();
    bool IsEquality = CC == ISD::SETEQ || CC == ISD::SETNE;
    bool IsSignTest =
        (Immediate == 0 && (CC == ISD::SETLT || CC == ISD::SETGE)) ||
        (Immediate == -1 && (CC == ISD::SETGT || CC == ISD::SETLE));
    if (!STI.isRC32() &&
        (IsSignTest ||
        (IsEquality && (Immediate == 0 ||
                        (!STI.isNano() && isInt<8>(Immediate)))))) {
      return DAG.getNode(
          RISCCISD::BR_CC_IMM, DL, MVT::Other, Op.getOperand(0),
          LHS, DAG.getConstant(APInt(STI.getXLenVT().getSizeInBits(),
                                     Immediate, true),
                             DL, STI.getXLenVT()),
          DAG.getTargetConstant(CC, DL, MVT::i16), Op.getOperand(4));
    }
  }
  return DAG.getNode(
      RISCCISD::BR_CC, DL, MVT::Other, Op.getOperand(0), LHS, RHS,
      DAG.getTargetConstant(CC, DL, MVT::i16),
      Op.getOperand(4));
}

SDValue RISCCTargetLowering::lowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  bool IsEquality = CC == ISD::SETEQ || CC == ISD::SETNE;
  if (IsEquality && isa<ConstantSDNode>(LHS) &&
      !isa<ConstantSDNode>(RHS))
    std::swap(LHS, RHS);
  if (IsEquality) {
    if (!STI.isRC32())
      if (auto *C = dyn_cast<ConstantSDNode>(RHS);
          C && isUInt<8>(C->getZExtValue())) {
      return DAG.getNode(
          RISCCISD::SET_CC_IMM, DL, STI.getXLenVT(), LHS,
          DAG.getConstant(C->getZExtValue(), DL, MVT::i16),
          DAG.getTargetConstant(CC, DL, MVT::i16));
      }
  }
  return DAG.getNode(
      RISCCISD::SET_CC, DL, STI.getXLenVT(), LHS, RHS,
      DAG.getTargetConstant(CC, DL, MVT::i16));
}

SDValue RISCCTargetLowering::lowerSELECTCC(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(
      RISCCISD::SELECT_CC, DL, Op.getValueType(), Op.getOperand(0), Op.getOperand(1),
      Op.getOperand(2), Op.getOperand(3),
      DAG.getTargetConstant(cast<CondCodeSDNode>(Op.getOperand(4))->get(), DL,
                            MVT::i16));
}

//===----------------------------------------------------------------------===//
// Calling convention lowering
//===----------------------------------------------------------------------===//

template <typename ArgT>
static std::pair<MVT, CCValAssign::LocInfo>
getArgumentLocation(const ArgT &Arg, MVT SlotVT) {
  if (Arg.VT != MVT::i1 && Arg.VT != MVT::i8 &&
      !(SlotVT == MVT::i32 && Arg.VT == MVT::i16))
    return {Arg.VT, CCValAssign::Full};

  CCValAssign::LocInfo Info =
      Arg.VT == MVT::i1 || Arg.Flags.isZExt()
          ? CCValAssign::ZExt
          : Arg.Flags.isSExt() ? CCValAssign::SExt : CCValAssign::AExt;
  return {SlotVT, Info};
}

template <typename ArgT>
static void analyzeArguments(CCState &State, SmallVectorImpl<CCValAssign> &Locs,
                             const SmallVectorImpl<ArgT> &Args, MVT SlotVT) {
  static const MCPhysReg ArgRegs[] = {RISCC::R1, RISCC::R2, RISCC::R3};
  unsigned NextReg = 0;
  bool OnStack = false;

  for (unsigned I = 0; I < Args.size();) {
    unsigned ArgEnd = I + 1;
    while (ArgEnd < Args.size() &&
           Args[ArgEnd].OrigArgIndex == Args[I].OrigArgIndex)
      ++ArgEnd;
    const unsigned NumParts = ArgEnd - I;
    const bool UseRegisters = !Args[I].Flags.isVarArg() && !OnStack &&
                              NextReg + NumParts <= std::size(ArgRegs);
    if (UseRegisters) {
      for (; I != ArgEnd; ++I) {
        auto [LocVT, LocInfo] = getArgumentLocation(Args[I], SlotVT);
        MCRegister R = State.AllocateReg(ArgRegs[NextReg++]);
        Locs.push_back(
            CCValAssign::getReg(I, Args[I].VT, R, LocVT, LocInfo));
      }
    } else {
      OnStack = true;
      for (; I != ArgEnd; ++I) {
        auto [LocVT, LocInfo] = getArgumentLocation(Args[I], SlotVT);
        if (SlotVT == MVT::i32)
          CC_RISCC32_Stack(I, Args[I].VT, LocVT, LocInfo, Args[I].Flags,
                           Args[I].OrigTy, State);
        else
          CC_RISCC_Stack(I, Args[I].VT, LocVT, LocInfo, Args[I].Flags,
                         Args[I].OrigTy, State);
      }
    }
  }
}

static bool isExpandedMulLibcall(
    const TargetLowering::CallLoweringInfo &CLI) {
  const auto *Callee = dyn_cast<ExternalSymbolSDNode>(CLI.Callee);
  if (!Callee || StringRef(Callee->getSymbol()) != "__mulsi3" ||
      CLI.Outs.size() != 4)
    return false;

  for (const auto [I, Arg] : enumerate(CLI.Outs))
    if (Arg.VT != MVT::i16 || Arg.OrigArgIndex != I || Arg.PartOffset != 0)
      return false;
  return true;
}

static SDValue unpackArgument(SDValue Value, const CCValAssign &VA,
                              const SDLoc &DL, SelectionDAG &DAG) {
  if (VA.getLocInfo() == CCValAssign::SExt)
    Value = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), Value,
                        DAG.getValueType(VA.getValVT()));
  else if (VA.getLocInfo() == CCValAssign::ZExt)
    Value = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), Value,
                        DAG.getValueType(VA.getValVT()));
  if (VA.getLocInfo() != CCValAssign::Full)
    Value = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Value);
  return Value;
}

SDValue RISCCTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CC, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (CC != CallingConv::C && CC != CallingConv::Fast)
    report_fatal_error("unsupported RISC-C calling convention");
  SmallVector<CCValAssign, 16> Locs;
  CCState State(CC, IsVarArg, DAG.getMachineFunction(), Locs, *DAG.getContext());
  analyzeArguments(State, Locs, Ins, STI.getXLenVT());
  MachineFunction &MF = DAG.getMachineFunction();
  if (STI.isNano()) {
    Register ReturnAddress =
        MF.addLiveIn(RISCC::R6, &RISCC::GPRNanoCallerRegClass);
    MF.getInfo<RISCCMachineFunctionInfo>()->setReturnAddressReg(ReturnAddress);
  }

  for (const CCValAssign &VA : Locs) {
    SDValue V;
    if (VA.isRegLoc()) {
      Register VR = MF.getRegInfo().createVirtualRegister(STI.getGPRClass());
      MF.getRegInfo().addLiveIn(VA.getLocReg(), VR);
      V = DAG.getCopyFromReg(Chain, DL, VR, VA.getLocVT());
    } else {
      int FI = MF.getFrameInfo().CreateFixedObject(STI.getSlotSize(),
                                                    VA.getLocMemOffset(), true);
      SDValue Addr = DAG.getFrameIndex(FI, STI.getXLenVT());
      V = DAG.getLoad(VA.getLocVT(), DL, Chain, Addr,
                      MachinePointerInfo::getFixedStack(MF, FI));
    }
    InVals.push_back(unpackArgument(V, VA, DL, DAG));
  }
  MachineFrameInfo &FrameInfo = MF.getFrameInfo();
  if (IsVarArg && FrameInfo.hasVAStart()) {
    // The varargs area begins after the fixed stack arguments.
    int VarArgsFI = FrameInfo.CreateFixedObject(STI.getSlotSize(),
                                                State.getStackSize(), true);
    MF.getInfo<RISCCMachineFunctionInfo>()->setVarArgsFrameIndex(VarArgsFI);
  }
  return Chain;
}

static bool isEligibleForSiblingCall(
    const TargetLowering::CallLoweringInfo &CLI, const MachineFunction &MF,
    unsigned StackBytes) {
  const Function &Caller = MF.getFunction();
  if (!isa<GlobalAddressSDNode, ExternalSymbolSDNode>(CLI.Callee) ||
      CLI.IsVarArg || Caller.isVarArg() || StackBytes != 0)
    return false;
  if (CLI.CallConv != Caller.getCallingConv() ||
      CLI.RetTy != Caller.getReturnType() ||
      Caller.hasFnAttribute("interrupt"))
    return false;
  if (llvm::any_of(CLI.Outs, [](const ISD::OutputArg &Arg) {
        return Arg.Flags.isByVal() || Arg.Flags.isSRet() ||
               Arg.Flags.isNest();
      }))
    return false;

  return true;
}

static MCRegister getDirectCalleeLink(SDValue Callee) {
  if (auto *Address = dyn_cast<GlobalAddressSDNode>(Callee))
    if (auto *F = dyn_cast<Function>(Address->getGlobal()))
      return getRISCCMainlineLinkRegister(*F);
  return RISCC::S7;
}

static bool isSRegPreservingCall(SDValue Callee) {
  const auto *Symbol = dyn_cast<ExternalSymbolSDNode>(Callee);
  return Symbol &&
         Symbol->getTargetFlags() == RISCCII::MO_SREG_PRESERVING_CALL;
}

static void copyMainlineLink(SelectionDAG &DAG, const SDLoc &DL,
                             Register From, Register To, SDValue &Chain,
                             SDValue &Glue) {
  // S registers cannot copy directly to one another. Route the link through
  // r0 so normal COPY expansion emits one MFS and one MTS.
  SDValue Link = DAG.getCopyFromReg(Chain, DL, From, MVT::i16, Glue);
  Chain = Link.getValue(1);
  Glue = Link.getValue(2);
  Chain = DAG.getCopyToReg(Chain, DL, RISCC::R0, Link, Glue);
  Glue = Chain.getValue(1);

  Link = DAG.getCopyFromReg(Chain, DL, RISCC::R0, MVT::i16, Glue);
  Chain = Link.getValue(1);
  Glue = Link.getValue(2);
  Chain = DAG.getCopyToReg(Chain, DL, To, Link, Glue);
  Glue = Chain.getValue(1);
}

SDValue RISCCTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {
  if (CLI.CallConv != CallingConv::C && CLI.CallConv != CallingConv::Fast)
    report_fatal_error("unsupported RISC-C calling convention");
  SelectionDAG &DAG = CLI.DAG;
  SDLoc DL = CLI.DL;
  MachineFunction &MF = DAG.getMachineFunction();
  auto &FuncInfo = *MF.getInfo<RISCCMachineFunctionInfo>();
  bool PreservesSRegs = !STI.isNano() && isSRegPreservingCall(CLI.Callee);
  SmallVector<CCValAssign, 16> Locs;
  CCState State(CLI.CallConv, CLI.IsVarArg, MF, Locs, *DAG.getContext());
  SmallVector<ISD::OutputArg, 16> Args(CLI.Outs.begin(), CLI.Outs.end());
  // Type legalization expands an i32 multiply libcall into four i16 values
  // without retaining its two source-argument groups. Recreate the groups so
  // __mulsi3 uses the normal no-split C ABI.
  if (isExpandedMulLibcall(CLI)) {
    Args[1].OrigArgIndex = 0;
    Args[2].OrigArgIndex = Args[3].OrigArgIndex = 1;
  }
  analyzeArguments(State, Locs, Args, STI.getXLenVT());
  unsigned NumBytes = State.getStackSize();
  if (!STI.isRC32() && NumBytes > 0xffff)
    report_fatal_error(
        "RISC-C outgoing call frame exceeds the 16-bit address space");

  if (CLI.IsTailCall)
    CLI.IsTailCall = isEligibleForSiblingCall(CLI, MF, NumBytes);
  if (!CLI.IsTailCall && CLI.CB && CLI.CB->isMustTailCall())
    report_fatal_error("failed to lower a mandatory RISC-C tail call");

  SDValue Chain = CLI.Chain;
  if (!CLI.IsTailCall) {
    MF.getFrameInfo().setAdjustsStack(true);
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);
  }
  SmallVector<std::pair<MCRegister, SDValue>, 4> RegArgs;
  SmallVector<SDValue, 8> Stores;

  for (unsigned I = 0; I != Locs.size(); ++I) {
    const CCValAssign &VA = Locs[I];
    SDValue V = CLI.OutVals[I];
    if (VA.getLocInfo() == CCValAssign::SExt)
      V = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), V);
    else if (VA.getLocInfo() == CCValAssign::ZExt)
      V = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), V);
    else if (VA.getLocInfo() == CCValAssign::AExt)
      V = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), V);
    if (VA.isRegLoc()) {
      RegArgs.emplace_back(VA.getLocReg(), V);
    } else {
      SDValue SP =
          DAG.getCopyFromReg(Chain, DL, RISCC::R7, STI.getXLenVT());
      SDValue Addr = DAG.getNode(ISD::ADD, DL, STI.getXLenVT(), SP,
                                 DAG.getConstant(VA.getLocMemOffset(), DL,
                                                 STI.getXLenVT()));
      Stores.push_back(DAG.getStore(Chain, DL, V, Addr, MachinePointerInfo()));
    }
  }
  if (!Stores.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
  SDValue Glue;
  for (auto [Reg, V] : RegArgs) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, V, Glue);
    Glue = Chain.getValue(1);
  }

  MCRegister CalleeLink = STI.isNano() || STI.isRC32()
                              ? RISCC::S7
                              : getDirectCalleeLink(CLI.Callee);
  bool UsesPrivateLink = !STI.isNano() && CalleeLink == RISCC::S3;

  if (CLI.IsTailCall && !STI.isNano()) {
    Register CallerLink = FuncInfo.getReturnAddressReg();
    if (CallerLink != Register(CalleeLink))
      copyMainlineLink(DAG, DL, CallerLink, CalleeLink, Chain, Glue);
  }
  if (CLI.IsTailCall && STI.isNano()) {
    Register ReturnAddress = FuncInfo.getReturnAddressReg();
    assert(ReturnAddress && "Nano return address was not initialized");
    SDValue SavedReturnAddress =
        DAG.getCopyFromReg(Chain, DL, ReturnAddress, MVT::i16, Glue);
    Chain = SavedReturnAddress.getValue(1);
    Glue = SavedReturnAddress.getValue(2);
    Chain =
        DAG.getCopyToReg(Chain, DL, RISCC::R6, SavedReturnAddress, Glue);
    Glue = Chain.getValue(1);
  }

  SDValue Callee = CLI.Callee;
  if (STI.isRC32()) {
    if (isa<GlobalAddressSDNode>(Callee))
      Callee = lowerGlobalAddress(Callee, DAG);
    else if (isa<ExternalSymbolSDNode>(Callee))
      Callee = lowerExternalSymbol(Callee, DAG);
  } else if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i16,
                                        G->getOffset(), RISCCII::MO_None);
  else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i16,
                                         RISCCII::MO_None);

  SmallVector<SDValue, 10> Ops{Chain, Callee};
  for (auto [Reg, V] : RegArgs)
    Ops.push_back(DAG.getRegister(Reg, V.getValueType()));
  const auto *TRI = STI.getRegisterInfo();
  const uint32_t *CallMask =
      PreservesSRegs ? TRI->getSRegPreservingCallMask()
                     : TRI->getCallPreservedMask(MF, CLI.CallConv);
  Ops.push_back(DAG.getRegisterMask(CallMask));
  if (Glue)
    Ops.push_back(Glue);
  unsigned CallOpcode =
      CLI.IsTailCall
          ? (UsesPrivateLink ? RISCCISD::TAIL_PRIVATE : RISCCISD::TAIL)
          : (UsesPrivateLink ? RISCCISD::CALL_PRIVATE : RISCCISD::CALL);
  Chain = DAG.getNode(CallOpcode, DL,
                      DAG.getVTList(MVT::Other, MVT::Glue), Ops);
  if (CLI.IsTailCall) {
    MF.getFrameInfo().setHasTailCall();
    return Chain;
  }
  Glue = Chain.getValue(1);
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);
  return lowerCallResult(Chain, Glue, CLI.CallConv, CLI.IsVarArg, CLI.Ins, DL,
                         DAG, InVals);
}

SDValue RISCCTargetLowering::lowerCallResult(
    SDValue Chain, SDValue Glue, CallingConv::ID CC, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  SmallVector<CCValAssign, 8> Locs;
  CCState State(CC, IsVarArg, DAG.getMachineFunction(), Locs, *DAG.getContext());
  State.AnalyzeFormalArguments(Ins,
                               STI.isRC32() ? RetCC_RISCC32 : RetCC_RISCC);
  for (unsigned I = 0; I != Locs.size(); ++I) {
    const CCValAssign &VA = Locs[I];
    SDValue V = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue);
    Chain = V.getValue(1);
    Glue = V.getValue(2);
    InVals.push_back(unpackArgument(V, VA, DL, DAG));
  }
  return Chain;
}

bool RISCCTargetLowering::CanLowerReturn(
    CallingConv::ID CC, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Ctx,
    const Type *) const {
  SmallVector<CCValAssign, 8> Locs;
  CCState State(CC, IsVarArg, MF, Locs, Ctx);
  return State.CheckReturn(Outs,
                           STI.isRC32() ? RetCC_RISCC32 : RetCC_RISCC);
}

SDValue RISCCTargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CC, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto &FuncInfo = *MF.getInfo<RISCCMachineFunctionInfo>();
  SmallVector<CCValAssign, 8> Locs;
  CCState State(CC, IsVarArg, MF, Locs, *DAG.getContext());
  State.AnalyzeReturn(Outs, STI.isRC32() ? RetCC_RISCC32 : RetCC_RISCC);
  SDValue Glue;
  SmallVector<SDValue, 8> Ops{Chain};
  if (STI.isNano()) {
    Register ReturnAddress = FuncInfo.getReturnAddressReg();
    assert(ReturnAddress && "Nano return address was not initialized");
    SDValue SavedReturnAddress =
        DAG.getCopyFromReg(Chain, DL, ReturnAddress, MVT::i16);
    Chain = SavedReturnAddress.getValue(1);
    Ops.push_back(SavedReturnAddress);
  }
  for (unsigned I = 0; I != Locs.size(); ++I) {
    SDValue V = OutVals[I];
    if (Locs[I].getLocInfo() != CCValAssign::Full) {
      unsigned Ext = Outs[I].Flags.isSExt() ? ISD::SIGN_EXTEND
                     : Outs[I].Flags.isZExt() ? ISD::ZERO_EXTEND
                                              : ISD::ANY_EXTEND;
      V = DAG.getNode(Ext, DL, Locs[I].getLocVT(), V);
    }
    Chain = DAG.getCopyToReg(Chain, DL, Locs[I].getLocReg(), V, Glue);
    Glue = Chain.getValue(1);
    Ops.push_back(DAG.getRegister(Locs[I].getLocReg(), Locs[I].getLocVT()));
  }
  Ops[0] = Chain;
  if (Glue)
    Ops.push_back(Glue);
  Register ReturnAddress = FuncInfo.getReturnAddressReg();
  unsigned RetOpcode = !STI.isNano() && ReturnAddress == RISCC::S3
                           ? RISCCISD::RET_PRIVATE_FLAG
                           : RISCCISD::RET_FLAG;
  return DAG.getNode(RetOpcode, DL, MVT::Other, Ops);
}

//===----------------------------------------------------------------------===//
// Custom machine-instruction insertion
//===----------------------------------------------------------------------===//

static Register createVirtualGPR(MachineBasicBlock &MBB) {
  return MBB.getParent()->getRegInfo().createVirtualRegister(
      MBB.getParent()->getSubtarget<RISCCSubtarget>().getGPRClass());
}

static bool isSignedRelationalComparison(ISD::CondCode CC) {
  return CC == ISD::SETLT || CC == ISD::SETGT || CC == ISD::SETLE ||
         CC == ISD::SETGE;
}

static bool swapsComparisonOperands(ISD::CondCode CC) {
  return CC == ISD::SETGT || CC == ISD::SETLE || CC == ISD::SETUGT ||
         CC == ISD::SETULE;
}

static bool invertsLessThanResult(ISD::CondCode CC) {
  return CC == ISD::SETGE || CC == ISD::SETLE || CC == ISD::SETUGE ||
         CC == ISD::SETULE;
}

static void biasSignedComparisonOperands(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator I,
                                         const DebugLoc &DL,
                                         const RISCCInstrInfo &TII,
                                         Register &LHS, Register &RHS) {
  Register SignMask = createVirtualGPR(MBB);
  Register BiasedLHS = createVirtualGPR(MBB);
  Register BiasedRHS = createVirtualGPR(MBB);
  TII.materializeImmediate(MBB, I, DL, SignMask, 0x8000);
  BuildMI(MBB, I, DL, TII.get(RISCC::XOR), BiasedLHS)
      .addReg(LHS)
      .addReg(SignMask);
  BuildMI(MBB, I, DL, TII.get(RISCC::XOR), BiasedRHS)
      .addReg(RHS)
      .addReg(SignMask);
  LHS = BiasedLHS;
  RHS = BiasedRHS;
}

static void emitComparisonBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, const RISCCInstrInfo &TII,
                                 Register LHS, Register RHS, ISD::CondCode CC,
                                 MachineBasicBlock *Target, bool IsNano,
                                 bool IsRC32) {
  if (swapsComparisonOperands(CC))
    std::swap(LHS, RHS);
  unsigned Cmp, Br;
  switch (CC) {
  case ISD::SETEQ:
    Cmp = IsRC32 ? RISCC::SUB32 : RISCC::SUB;
    Br = RISCC::BEQZ;
    break;
  case ISD::SETNE:
    Cmp = IsRC32 ? RISCC::SUB32 : RISCC::SUB;
    Br = RISCC::BNEZ;
    break;
  case ISD::SETLT:
  case ISD::SETGT:
    Cmp = IsRC32 ? RISCC::SLT32 : IsNano ? RISCC::SLTU : RISCC::SLT;
    Br = RISCC::BNEZ;
    break;
  case ISD::SETGE:
  case ISD::SETLE:
    Cmp = IsRC32 ? RISCC::SLT32 : IsNano ? RISCC::SLTU : RISCC::SLT;
    Br = RISCC::BEQZ;
    break;
  case ISD::SETULT:
  case ISD::SETUGT:
    Cmp = IsRC32 ? RISCC::SLTU32 : RISCC::SLTU;
    Br = RISCC::BNEZ;
    break;
  case ISD::SETUGE:
  case ISD::SETULE:
    Cmp = IsRC32 ? RISCC::SLTU32 : RISCC::SLTU;
    Br = RISCC::BEQZ;
    break;
  default:
    llvm_unreachable("unsupported integer condition");
  }
  if (IsNano && isSignedRelationalComparison(CC))
    biasSignedComparisonOperands(MBB, I, DL, TII, LHS, RHS);
  BuildMI(MBB, I, DL, TII.get(Cmp), RISCC::R0).addReg(LHS).addReg(RHS);
  BuildMI(MBB, I, DL, TII.get(Br)).addMBB(Target);
}

static void emitImmediateComparisonBranch(
    MachineInstr &MI, MachineBasicBlock &MBB, const RISCCInstrInfo &TII) {
  Register LHS = MI.getOperand(0).getReg();
  int64_t RHS = MI.getOperand(1).getImm();
  ISD::CondCode CC = ISD::CondCode(MI.getOperand(2).getImm());
  unsigned Branch;
  bool IsSignTest = RHS == 0 || (RHS == -1 &&
                                 (CC == ISD::SETGT || CC == ISD::SETLE));
  if (IsSignTest) {
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCC::MOV), RISCC::R0)
        .addReg(LHS);
    switch (CC) {
    case ISD::SETEQ:
      Branch = RISCC::BEQZ;
      break;
    case ISD::SETNE:
      Branch = RISCC::BNEZ;
      break;
    case ISD::SETLT:
    case ISD::SETLE:
      Branch = RISCC::BLTZ;
      break;
    case ISD::SETGE:
    case ISD::SETGT:
      Branch = RISCC::BGEZ;
      break;
    default:
      llvm_unreachable("unsupported sign comparison");
    }
  } else {
    assert((CC == ISD::SETEQ || CC == ISD::SETNE) &&
           "unsupported immediate comparison");
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCC::CMPI))
        .addReg(LHS)
        .addImm(RHS);
    Branch = CC == ISD::SETEQ ? RISCC::BEQZ : RISCC::BNEZ;
  }
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(Branch))
      .addMBB(MI.getOperand(3).getMBB());
}

static void emitNonZeroResult(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I,
                              const DebugLoc &DL,
                              const RISCCInstrInfo &TII, Register Destination,
                              Register Value, bool Invert, bool IsRC32) {
  Register Zero = createVirtualGPR(MBB);
  Register NonZero = Invert ? createVirtualGPR(MBB) : Destination;
  TII.materializeImmediate(MBB, I, DL, Zero, 0);
  BuildMI(MBB, I, DL, TII.get(IsRC32 ? RISCC::SLTU32 : RISCC::SLTU), NonZero)
      .addReg(Zero)
      .addReg(Value);
  if (Invert)
    BuildMI(MBB, I, DL, TII.get(IsRC32 ? RISCC::XORI32 : RISCC::XORI), Destination)
        .addReg(NonZero)
        .addImm(1);
}

static void emitComparisonResult(MachineInstr &MI, MachineBasicBlock &MBB,
                                 const RISCCInstrInfo &TII, bool IsNano,
                                 bool IsRC32) {
  ISD::CondCode CC = ISD::CondCode(MI.getOperand(3).getImm());
  Register Destination = MI.getOperand(0).getReg();
  Register LHS = MI.getOperand(1).getReg();
  Register RHS = MI.getOperand(2).getReg();
  if (CC == ISD::SETEQ || CC == ISD::SETNE) {
    Register Difference = createVirtualGPR(MBB);
    BuildMI(MBB, MI, MI.getDebugLoc(),
            TII.get(IsRC32 ? RISCC::XOR32 : RISCC::XOR), Difference)
        .addReg(LHS)
        .addReg(RHS);
    emitNonZeroResult(MBB, MI, MI.getDebugLoc(), TII, Destination, Difference,
                      CC == ISD::SETEQ, IsRC32);
    MI.eraseFromParent();
    return;
  }

  bool IsSigned = isSignedRelationalComparison(CC);
  assert((IsSigned || CC == ISD::SETULT || CC == ISD::SETUGT ||
          CC == ISD::SETULE || CC == ISD::SETUGE) &&
         "unsupported comparison");
  if (swapsComparisonOperands(CC))
    std::swap(LHS, RHS);
  if (IsNano && IsSigned)
    biasSignedComparisonOperands(MBB, MI, MI.getDebugLoc(), TII, LHS, RHS);

  Register Less = Destination;
  bool Invert = invertsLessThanResult(CC);
  if (Invert)
    Less = createVirtualGPR(MBB);
  unsigned Compare = IsRC32 ? (IsSigned ? RISCC::SLT32 : RISCC::SLTU32)
                             : IsSigned && !IsNano ? RISCC::SLT : RISCC::SLTU;
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(Compare), Less)
      .addReg(LHS)
      .addReg(RHS);
  if (Invert)
    BuildMI(MBB, MI, MI.getDebugLoc(),
            TII.get(IsRC32 ? RISCC::XORI32 : RISCC::XORI), Destination)
        .addReg(Less)
        .addImm(1);
  MI.eraseFromParent();
}

static void emitImmediateEqualityResult(MachineInstr &MI,
                                        MachineBasicBlock &MBB,
                                        const RISCCInstrInfo &TII) {
  Register Destination = MI.getOperand(0).getReg();
  Register LHS = MI.getOperand(1).getReg();
  uint64_t RHS = MI.getOperand(2).getImm();
  ISD::CondCode CC = ISD::CondCode(MI.getOperand(3).getImm());
  assert((CC == ISD::SETEQ || CC == ISD::SETNE) &&
         "immediate setcc only supports equality");

  Register Difference = LHS;
  if (RHS != 0) {
    Difference = createVirtualGPR(MBB);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCC::XORI), Difference)
        .addReg(LHS)
        .addImm(RHS);
  }
  emitNonZeroResult(MBB, MI, MI.getDebugLoc(), TII, Destination, Difference,
                    CC == ISD::SETEQ, false);
  MI.eraseFromParent();
}

static MachineBasicBlock *emitVariableShift(MachineInstr &MI,
                                            MachineBasicBlock *MBB,
                                            const RISCCInstrInfo &TII,
                                            bool HasWideShift) {
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  const BasicBlock *IRBlock = MBB->getBasicBlock();
  auto InsertAt = std::next(MBB->getIterator());
  MachineBasicBlock *Loop = MF->CreateMachineBasicBlock(IRBlock);
  MachineBasicBlock *Remainder = MF->CreateMachineBasicBlock(IRBlock);
  MF->insert(InsertAt, Loop);
  MF->insert(InsertAt, Remainder);
  Remainder->splice(Remainder->begin(), MBB, std::next(MI.getIterator()),
                    MBB->end());
  Remainder->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(Loop);
  MBB->addSuccessor(Remainder);
  Loop->addSuccessor(Loop);
  Loop->addSuccessor(Remainder);

  MachineRegisterInfo &MRI = MF->getRegInfo();
  Register ShiftPhi = MRI.createVirtualRegister(&RISCC::GPRRegClass);
  Register ShiftNext = MRI.createVirtualRegister(&RISCC::GPRRegClass);
  Register AmountPhi = MRI.createVirtualRegister(&RISCC::GPRRegClass);
  Register AmountNext = MRI.createVirtualRegister(&RISCC::GPRRegClass);
  Register Destination = MI.getOperand(0).getReg();
  Register Source = MI.getOperand(1).getReg();
  Register Amount = MI.getOperand(2).getReg();

  BuildMI(*MBB, MI, DL, TII.get(RISCC::MOV), RISCC::R0).addReg(Amount);
  BuildMI(*MBB, MI, DL, TII.get(RISCC::BEQZ)).addMBB(Remainder);

  BuildMI(*Loop, Loop->end(), DL, TII.get(TargetOpcode::PHI), ShiftPhi)
      .addReg(Source)
      .addMBB(MBB)
      .addReg(ShiftNext)
      .addMBB(Loop);
  BuildMI(*Loop, Loop->end(), DL, TII.get(TargetOpcode::PHI), AmountPhi)
      .addReg(Amount)
      .addMBB(MBB)
      .addReg(AmountNext)
      .addMBB(Loop);
  if (MI.getOpcode() == RISCC::PseudoSHL && !HasWideShift)
    BuildMI(*Loop, Loop->end(), DL, TII.get(RISCC::ADD), ShiftNext)
        .addReg(ShiftPhi)
        .addReg(ShiftPhi);
  else {
    const unsigned Opcode = MI.getOpcode() == RISCC::PseudoSHL   ? RISCC::SLLI
                            : MI.getOpcode() == RISCC::PseudoSRL ? RISCC::SRLI
                                                                : RISCC::SRAI;
    BuildMI(*Loop, Loop->end(), DL, TII.get(Opcode), ShiftNext)
        .addReg(ShiftPhi)
        .addImm(1);
  }
  BuildMI(*Loop, Loop->end(), DL, TII.get(RISCC::ADDI), AmountNext)
      .addReg(AmountPhi)
      .addImm(-1);
  BuildMI(*Loop, Loop->end(), DL, TII.get(RISCC::MOV), RISCC::R0)
      .addReg(AmountNext);
  BuildMI(*Loop, Loop->end(), DL, TII.get(RISCC::BNEZ)).addMBB(Loop);
  BuildMI(*Remainder, Remainder->begin(), DL, TII.get(TargetOpcode::PHI),
          Destination)
      .addReg(Source)
      .addMBB(MBB)
      .addReg(ShiftNext)
      .addMBB(Loop);
  MI.eraseFromParent();
  return Remainder;
}

static MachineBasicBlock *emitSelect(MachineInstr &MI,
                                     MachineBasicBlock *MBB,
                                     const RISCCInstrInfo &TII, bool IsNano,
                                     bool IsRC32) {
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  const BasicBlock *IRBlock = MBB->getBasicBlock();
  auto InsertAt = std::next(MBB->getIterator());
  MachineBasicBlock *True = MF->CreateMachineBasicBlock(IRBlock);
  MachineBasicBlock *Sink = MF->CreateMachineBasicBlock(IRBlock);
  MF->insert(InsertAt, True);
  MF->insert(InsertAt, Sink);
  Sink->splice(Sink->begin(), MBB, std::next(MI.getIterator()), MBB->end());
  Sink->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(True);
  MBB->addSuccessor(Sink);
  True->addSuccessor(Sink);

  Register Destination = MI.getOperand(0).getReg();
  Register LHS = MI.getOperand(1).getReg();
  Register RHS = MI.getOperand(2).getReg();
  Register TrueValue = MI.getOperand(3).getReg();
  Register FalseValue = MI.getOperand(4).getReg();
  emitComparisonBranch(*MBB, MI, DL, TII, LHS, RHS,
                       ISD::CondCode(MI.getOperand(5).getImm()), True, IsNano,
                       IsRC32);
  BuildMI(*MBB, MI, DL, TII.get(RISCC::JMP8)).addMBB(Sink);
  BuildMI(*Sink, Sink->begin(), DL, TII.get(TargetOpcode::PHI), Destination)
      .addReg(FalseValue)
      .addMBB(MBB)
      .addReg(TrueValue)
      .addMBB(True);
  MI.eraseFromParent();
  return Sink;
}

MachineBasicBlock *RISCCTargetLowering::EmitInstrWithCustomInserter(
    MachineInstr &MI, MachineBasicBlock *MBB) const {
  const auto &TII = *STI.getInstrInfo();
  switch (MI.getOpcode()) {
  case RISCC::PseudoSHL:
  case RISCC::PseudoSRL:
  case RISCC::PseudoSRA:
    return emitVariableShift(MI, MBB, TII, STI.hasWideShift());
  case RISCC::PseudoBRCC:
  case RISCC::PseudoBRCC32:
    emitComparisonBranch(*MBB, MI, MI.getDebugLoc(), TII,
                         MI.getOperand(0).getReg(), MI.getOperand(1).getReg(),
                         ISD::CondCode(MI.getOperand(2).getImm()),
                         MI.getOperand(3).getMBB(), STI.isNano(), STI.isRC32());
    MI.eraseFromParent();
    return MBB;
  case RISCC::PseudoBRCCImm:
    emitImmediateComparisonBranch(MI, *MBB, TII);
    MI.eraseFromParent();
    return MBB;
  case RISCC::PseudoSETCC:
  case RISCC::PseudoSETCC32:
    emitComparisonResult(MI, *MBB, TII, STI.isNano(), STI.isRC32());
    return MBB;
  case RISCC::PseudoSETCCImm:
    emitImmediateEqualityResult(MI, *MBB, TII);
    return MBB;
  case RISCC::PseudoSELECTCC:
  case RISCC::PseudoSELECTCC32:
    return emitSelect(MI, MBB, TII, STI.isNano(), STI.isRC32());
  default:
    llvm_unreachable("unexpected custom inserter opcode");
  }
}

//===----------------------------------------------------------------------===//
// Inline assembly
//===----------------------------------------------------------------------===//

TargetLowering::ConstraintType
RISCCTargetLowering::getConstraintType(StringRef C) const {
  if (C == "r")
    return C_RegisterClass;
  return TargetLowering::getConstraintType(C);
}

std::pair<unsigned, const TargetRegisterClass *>
RISCCTargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef C, MVT VT) const {
  if (C == "r" && VT.isInteger())
    return {0, STI.getGPRClass()};
  return TargetLowering::getRegForInlineAsmConstraint(TRI, C, VT);
}
