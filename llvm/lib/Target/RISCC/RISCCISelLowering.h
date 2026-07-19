//===-- RISCCISelLowering.h - RISCC DAG Lowering ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCISELLOWERING_H
#define LLVM_LIB_TARGET_RISCC_RISCCISELLOWERING_H

#include "RISCC.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class RISCCSubtarget;

namespace RISCCISD {
enum NodeType {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  RET_FLAG,
  RET_PRIVATE_FLAG,
  CALL,
  CALL_PRIVATE,
  TAIL,
  TAIL_PRIVATE,
  Wrapper,
  BR_CC,
  BR_CC_IMM,
  SET_CC,
  SET_CC_IMM,
  SELECT_CC,
  SHL,
  SRL,
  SRA,
  FSL1,
  FSR1,
};
}

class RISCCTargetLowering final : public TargetLowering {
  const RISCCSubtarget &STI;

  SDValue lowerGlobalAddress(SDValue, SelectionDAG &) const;
  SDValue lowerGlobalTLSAddress(SDValue, SelectionDAG &) const;
  SDValue lowerExternalSymbol(SDValue, SelectionDAG &) const;
  SDValue lowerBlockAddress(SDValue, SelectionDAG &) const;
  SDValue lowerAddrSpaceCast(SDValue, SelectionDAG &) const;
  SDValue lowerBRCC(SDValue, SelectionDAG &) const;
  SDValue lowerSETCC(SDValue, SelectionDAG &) const;
  SDValue lowerSELECTCC(SDValue, SelectionDAG &) const;
  SDValue lowerShift(SDValue, SelectionDAG &) const;
  SDValue lowerFunnelShift(SDValue, SelectionDAG &) const;
  SDValue lowerShiftLibCall(SDValue, unsigned Opcode, unsigned Amount,
                            SelectionDAG &) const;
  SDValue lowerMULLOHI(SDValue, SelectionDAG &, bool Signed) const;
  SDValue lowerVASTART(SDValue, SelectionDAG &) const;
  SDValue lowerCallResult(SDValue, SDValue, CallingConv::ID, bool,
                          const SmallVectorImpl<ISD::InputArg> &,
                          const SDLoc &, SelectionDAG &,
                          SmallVectorImpl<SDValue> &) const;

public:
  RISCCTargetLowering(const TargetMachine &, const RISCCSubtarget &);
  SDValue LowerOperation(SDValue, SelectionDAG &) const override;
  SDValue PerformDAGCombine(SDNode *, DAGCombinerInfo &) const override;
  SDValue LowerFormalArguments(
      SDValue, CallingConv::ID, bool,
      const SmallVectorImpl<ISD::InputArg> &, const SDLoc &, SelectionDAG &,
      SmallVectorImpl<SDValue> &) const override;
  SDValue LowerCall(CallLoweringInfo &,
                    SmallVectorImpl<SDValue> &) const override;
  bool CanLowerReturn(CallingConv::ID, MachineFunction &, bool,
                      const SmallVectorImpl<ISD::OutputArg> &, LLVMContext &,
                      const Type *) const override;
  SDValue LowerReturn(SDValue, CallingConv::ID, bool,
                      const SmallVectorImpl<ISD::OutputArg> &,
                      const SmallVectorImpl<SDValue> &, const SDLoc &,
                      SelectionDAG &) const override;
  MachineBasicBlock *EmitInstrWithCustomInserter(
      MachineInstr &, MachineBasicBlock *) const override;

  MVT getScalarShiftAmountTy(const DataLayout &, EVT) const override {
    return MVT::i16;
  }
  MVT::SimpleValueType getCmpLibcallReturnType() const override {
    return MVT::i16;
  }
  ConstraintType getConstraintType(StringRef) const override;
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *, StringRef,
                               MVT) const override;
};
}

#endif
