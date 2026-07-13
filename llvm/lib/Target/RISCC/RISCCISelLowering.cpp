#include "RISCCISelLowering.h"
#include "RISCCInstrInfo.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSubtarget.h"
#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <climits>

using namespace llvm;

#define GET_CALLING_CONV_IMPL
#include "RISCCGenCallingConv.inc"

RISCCTargetLowering::RISCCTargetLowering(const TargetMachine &TM,
                                         const RISCCSubtarget &STI)
    : TargetLowering(TM, STI), STI(STI) {
  addRegisterClass(MVT::i16, &RISCC::GPRRegClass);
  computeRegisterProperties(STI.getRegisterInfo());
  setStackPointerRegisterToSaveRestore(RISCC::R7);
  setBooleanContents(ZeroOrOneBooleanContent);
  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));
  setMaxAtomicSizeInBitsSupported(0);
  setMinimumJumpTableEntries(UINT_MAX);

  for (unsigned Op : {ISD::ADD, ISD::SUB, ISD::AND, ISD::OR, ISD::XOR,
                      ISD::MUL})
    setOperationAction(Op, MVT::i16, Legal);
  for (unsigned Op : {ISD::SHL, ISD::SRL, ISD::SRA})
    setOperationAction(Op, MVT::i16, Custom);
  // These multi-result nodes have no native instruction.  Marking them
  // Expand (rather than leaving the default Legal action) makes illegal wide
  // variable shifts use the mapped __*si3/__*di3 runtime helpers.
  for (unsigned Op : {ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS})
    setOperationAction(Op, MVT::i16, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i16, Custom);
  setOperationAction(ISD::SMUL_LOHI, MVT::i16, Custom);
  setOperationAction(ISD::MULHU, MVT::i16, Expand);
  setOperationAction(ISD::MULHS, MVT::i16, Expand);
  for (unsigned Op : {ISD::ROTL, ISD::ROTR})
    setOperationAction(Op, MVT::i16, Expand);
  for (unsigned Op : {ISD::BSWAP, ISD::CTLZ, ISD::CTTZ, ISD::CTPOP,
                      ISD::SIGN_EXTEND_INREG})
    setOperationAction(Op, MVT::i16, Expand);
  for (unsigned Op : {ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM})
    setOperationAction(Op, MVT::i16, LibCall);
  for (MVT VT : {MVT::i16, MVT::i32, MVT::i64}) {
    setOperationAction(ISD::SDIVREM, VT, Expand);
    setOperationAction(ISD::UDIVREM, VT, Expand);
  }
  for (MVT VT : {MVT::i32, MVT::i64}) {
    for (unsigned Op : {ISD::MUL, ISD::SHL, ISD::SRL, ISD::SRA,
                        ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM})
      setOperationAction(Op, VT, LibCall);
  }

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
  setLoadExtAction(ISD::SEXTLOAD, MVT::i16, MVT::i8, Legal);
  setTruncStoreAction(MVT::i16, MVT::i8, Legal);
}

SDValue RISCCTargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress: return lowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress: return lowerGlobalTLSAddress(Op, DAG);
  case ISD::ExternalSymbol: return lowerExternalSymbol(Op, DAG);
  case ISD::BlockAddress: return lowerBlockAddress(Op, DAG);
  case ISD::ADDRSPACECAST: return lowerAddrSpaceCast(Op, DAG);
  case ISD::BR_CC: return lowerBRCC(Op, DAG);
  case ISD::SETCC: return lowerSETCC(Op, DAG);
  case ISD::SELECT_CC: return lowerSELECTCC(Op, DAG);
  case ISD::SHL: case ISD::SRL: case ISD::SRA:
    return lowerShift(Op, DAG);
  case ISD::UMUL_LOHI: return lowerMULLOHI(Op, DAG, false);
  case ISD::SMUL_LOHI: return lowerMULLOHI(Op, DAG, true);
  case ISD::VASTART: return lowerVASTART(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC: {
    const Function &Fn = DAG.getMachineFunction().getFunction();
    DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
        Fn, "RISC-C does not support dynamic stack allocation",
        SDLoc(Op).getDebugLoc()));
    SDValue Results[] = {
        DAG.getConstant(0, SDLoc(Op), Op.getValueType()), Op.getOperand(0)};
    return DAG.getMergeValues(Results, SDLoc(Op));
  }
  default: llvm_unreachable("unexpected custom RISC-C lowering");
  }
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
  SDValue C8 = DAG.getConstant(8, DL, MVT::i16);
  SDValue C15 = DAG.getConstant(15, DL, MVT::i16);
  SDValue ByteMask = DAG.getConstant(0xff, DL, MVT::i16);
  SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);

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

SDValue RISCCTargetLowering::lowerShift(SDValue Op,
                                        SelectionDAG &DAG) const {
  unsigned TOpc = Op.getOpcode() == ISD::SHL ? RISCCISD::SHL
                  : Op.getOpcode() == ISD::SRL ? RISCCISD::SRL
                                               : RISCCISD::SRA;
  SDLoc DL(Op);
  if (const auto *C = dyn_cast<ConstantSDNode>(Op.getOperand(1))) {
    unsigned Amount = C->getZExtValue() & 15;
    SDValue V = Op.getOperand(0);
    while (Amount) {
      unsigned Chunk = std::min(Amount, 8u);
      V = DAG.getNode(TOpc, DL, MVT::i16, V,
                      DAG.getConstant(Chunk, DL, MVT::i16));
      Amount -= Chunk;
    }
    return V;
  }
  return DAG.getNode(TOpc, DL, MVT::i16, Op.getOperand(0), Op.getOperand(1));
}

SDValue RISCCTargetLowering::lowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  auto *N = cast<GlobalAddressSDNode>(Op);
  unsigned TF = (isa<Function>(N->getGlobal()) ||
                 N->getGlobal()->getAddressSpace() == 1)
                    ? RISCCII::MO_CODE : RISCCII::MO_None;
  SDValue T = DAG.getTargetGlobalAddress(N->getGlobal(), SDLoc(Op),
                                         Op.getValueType(), N->getOffset(), TF);
  return DAG.getNode(RISCCISD::Wrapper, SDLoc(Op), Op.getValueType(), T);
}

SDValue RISCCTargetLowering::lowerGlobalTLSAddress(SDValue Op,
                                                    SelectionDAG &DAG) const {
  const auto *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  SDLoc DL(Op);
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
  SDValue T = DAG.getTargetExternalSymbol(N->getSymbol(), Op.getValueType(),
                                          RISCCII::MO_None);
  return DAG.getNode(RISCCISD::Wrapper, SDLoc(Op), Op.getValueType(), T);
}

SDValue RISCCTargetLowering::lowerBlockAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  auto *N = cast<BlockAddressSDNode>(Op);
  SDValue T = DAG.getTargetBlockAddress(N->getBlockAddress(), Op.getValueType(),
                                        0, RISCCII::MO_CODE);
  return DAG.getNode(RISCCISD::Wrapper, SDLoc(Op), Op.getValueType(), T);
}

SDValue RISCCTargetLowering::lowerAddrSpaceCast(SDValue Op,
                                                SelectionDAG &DAG) const {
  auto *N = cast<AddrSpaceCastSDNode>(Op);
  SDValue V = Op.getOperand(0);
  SDValue One = DAG.getConstant(1, SDLoc(Op), MVT::i16);
  if (N->getSrcAddressSpace() == 0 && N->getDestAddressSpace() == 1)
    return DAG.getNode(ISD::SRL, SDLoc(Op), MVT::i16, V, One);
  if (N->getSrcAddressSpace() == 1 && N->getDestAddressSpace() == 0)
    return DAG.getNode(ISD::SHL, SDLoc(Op), MVT::i16, V, One);
  return V;
}

SDValue RISCCTargetLowering::lowerBRCC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(
      RISCCISD::BR_CC, DL, MVT::Other, Op.getOperand(0), Op.getOperand(2),
      Op.getOperand(3),
      DAG.getConstant(cast<CondCodeSDNode>(Op.getOperand(1))->get(), DL,
                      MVT::i16),
      Op.getOperand(4));
}

SDValue RISCCTargetLowering::lowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(
      RISCCISD::SET_CC, DL, MVT::i16, Op.getOperand(0), Op.getOperand(1),
      DAG.getConstant(cast<CondCodeSDNode>(Op.getOperand(2))->get(), DL,
                      MVT::i16));
}

SDValue RISCCTargetLowering::lowerSELECTCC(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(
      RISCCISD::SELECT_CC, DL, MVT::i16, Op.getOperand(0), Op.getOperand(1),
      Op.getOperand(2), Op.getOperand(3),
      DAG.getConstant(cast<CondCodeSDNode>(Op.getOperand(4))->get(), DL,
                      MVT::i16));
}

template <typename ArgT>
static void analyzeArguments(CCState &State, SmallVectorImpl<CCValAssign> &Locs,
                             const SmallVectorImpl<ArgT> &Args) {
  static const MCPhysReg ArgRegs[] = {RISCC::R1, RISCC::R2, RISCC::R3,
                                      RISCC::R4};
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
        MVT VT = Args[I].VT, LocVT = VT;
        CCValAssign::LocInfo LI = CCValAssign::Full;
        if (VT == MVT::i1 || VT == MVT::i8) {
          LocVT = MVT::i16;
          LI = VT == MVT::i1 || Args[I].Flags.isZExt()
                   ? CCValAssign::ZExt
                   : Args[I].Flags.isSExt() ? CCValAssign::SExt
                                            : CCValAssign::AExt;
        }
        MCRegister R = State.AllocateReg(ArgRegs[NextReg++]);
        Locs.push_back(CCValAssign::getReg(I, VT, R, LocVT, LI));
      }
    } else {
      OnStack = true;
      for (; I != ArgEnd; ++I) {
        MVT VT = Args[I].VT, LocVT = VT;
        CCValAssign::LocInfo LI = CCValAssign::Full;
        if (VT == MVT::i1 || VT == MVT::i8) {
          LocVT = MVT::i16;
          LI = VT == MVT::i1 || Args[I].Flags.isZExt()
                   ? CCValAssign::ZExt
                   : Args[I].Flags.isSExt() ? CCValAssign::SExt
                                            : CCValAssign::AExt;
        }
        CC_RISCC_Stack(I, VT, LocVT, LI, Args[I].Flags, Args[I].OrigTy, State);
      }
    }
  }
}

SDValue RISCCTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CC, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (CC != CallingConv::C && CC != CallingConv::Fast)
    report_fatal_error("unsupported RISC-C calling convention");
  SmallVector<CCValAssign, 16> Locs;
  CCState State(CC, IsVarArg, DAG.getMachineFunction(), Locs, *DAG.getContext());
  analyzeArguments(State, Locs, Ins);
  MachineFunction &MF = DAG.getMachineFunction();

  for (const CCValAssign &VA : Locs) {
    SDValue V;
    if (VA.isRegLoc()) {
      Register VR = MF.getRegInfo().createVirtualRegister(&RISCC::GPRRegClass);
      MF.getRegInfo().addLiveIn(VA.getLocReg(), VR);
      V = DAG.getCopyFromReg(Chain, DL, VR, VA.getLocVT());
    } else {
      int FI = MF.getFrameInfo().CreateFixedObject(2, VA.getLocMemOffset(), true);
      SDValue Addr = DAG.getFrameIndex(FI, MVT::i16);
      V = DAG.getLoad(VA.getLocVT(), DL, Chain, Addr,
                      MachinePointerInfo::getFixedStack(MF, FI));
    }
    if (VA.getLocInfo() == CCValAssign::SExt)
      V = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), V,
                      DAG.getValueType(VA.getValVT()));
    else if (VA.getLocInfo() == CCValAssign::ZExt)
      V = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), V,
                      DAG.getValueType(VA.getValVT()));
    if (VA.getLocInfo() != CCValAssign::Full)
      V = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), V);
    InVals.push_back(V);
  }
  MachineFrameInfo &FrameInfo = MF.getFrameInfo();
  if (IsVarArg && FrameInfo.hasVAStart()) {
    // The varargs area begins after the fixed stack arguments.
    int VarArgsFI = FrameInfo.CreateFixedObject(2, State.getStackSize(), true);
    MF.getInfo<RISCCMachineFunctionInfo>()->setVarArgsFrameIndex(VarArgsFI);
  }
  return Chain;
}

SDValue RISCCTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {
  CLI.IsTailCall = false;
  if (CLI.CallConv != CallingConv::C && CLI.CallConv != CallingConv::Fast)
    report_fatal_error("unsupported RISC-C calling convention");
  SelectionDAG &DAG = CLI.DAG;
  SDLoc DL = CLI.DL;
  SmallVector<CCValAssign, 16> Locs;
  CCState State(CLI.CallConv, CLI.IsVarArg, DAG.getMachineFunction(), Locs,
                *DAG.getContext());
  analyzeArguments(State, Locs, CLI.Outs);
  unsigned NumBytes = State.getStackSize();
  if (NumBytes > 0xffff)
    report_fatal_error(
        "RISC-C outgoing call frame exceeds the 16-bit address space");
  DAG.getMachineFunction().getFrameInfo().setAdjustsStack(true);
  SDValue Chain = DAG.getCALLSEQ_START(CLI.Chain, NumBytes, 0, DL);
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
      SDValue SP = DAG.getCopyFromReg(Chain, DL, RISCC::R7, MVT::i16);
      SDValue Addr = DAG.getNode(ISD::ADD, DL, MVT::i16, SP,
                                 DAG.getConstant(VA.getLocMemOffset(), DL,
                                                 MVT::i16));
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

  SDValue Callee = CLI.Callee;
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i16,
                                        G->getOffset(), RISCCII::MO_CODE);
  else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i16,
                                         RISCCII::MO_CODE);

  SmallVector<SDValue, 10> Ops{Chain, Callee};
  for (auto [Reg, V] : RegArgs)
    Ops.push_back(DAG.getRegister(Reg, V.getValueType()));
  Ops.push_back(DAG.getRegisterMask(
      STI.getRegisterInfo()->getCallPreservedMask(DAG.getMachineFunction(),
                                                   CLI.CallConv)));
  if (Glue) Ops.push_back(Glue);
  Chain = DAG.getNode(RISCCISD::CALL, DL,
                      DAG.getVTList(MVT::Other, MVT::Glue), Ops);
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
  State.AnalyzeCallResult(Ins, RetCC_RISCC);
  for (unsigned I = 0; I != Locs.size(); ++I) {
    const CCValAssign &VA = Locs[I];
    SDValue V = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue);
    Chain = V.getValue(1); Glue = V.getValue(2);
    if (VA.getLocInfo() == CCValAssign::SExt)
      V = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), V,
                      DAG.getValueType(VA.getValVT()));
    else if (VA.getLocInfo() == CCValAssign::ZExt)
      V = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), V,
                      DAG.getValueType(VA.getValVT()));
    if (VA.getLocInfo() != CCValAssign::Full)
      V = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), V);
    InVals.push_back(V);
  }
  return Chain;
}

bool RISCCTargetLowering::CanLowerReturn(
    CallingConv::ID CC, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Ctx,
    const Type *) const {
  SmallVector<CCValAssign, 8> Locs;
  CCState State(CC, IsVarArg, MF, Locs, Ctx);
  return State.CheckReturn(Outs, RetCC_RISCC);
}

SDValue RISCCTargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CC, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 8> Locs;
  CCState State(CC, IsVarArg, DAG.getMachineFunction(), Locs, *DAG.getContext());
  State.AnalyzeReturn(Outs, RetCC_RISCC);
  SDValue Glue;
  SmallVector<SDValue, 8> Ops{Chain};
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
  if (Glue) Ops.push_back(Glue);
  return DAG.getNode(RISCCISD::RET_FLAG, DL, MVT::Other, Ops);
}

static void emitComparisonBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, const RISCCInstrInfo &TII,
                                 Register LHS, Register RHS, ISD::CondCode CC,
                                 MachineBasicBlock *Target) {
  bool Swap = CC == ISD::SETGT || CC == ISD::SETLE ||
              CC == ISD::SETUGT || CC == ISD::SETULE;
  if (Swap) std::swap(LHS, RHS);
  unsigned Cmp, Br;
  switch (CC) {
  case ISD::SETEQ: Cmp = RISCC::SUB; Br = RISCC::BEQZ; break;
  case ISD::SETNE: Cmp = RISCC::SUB; Br = RISCC::BNEZ; break;
  case ISD::SETLT: case ISD::SETGT:
    Cmp = RISCC::SLT; Br = RISCC::BNEZ; break;
  case ISD::SETGE: case ISD::SETLE:
    Cmp = RISCC::SLT; Br = RISCC::BEQZ; break;
  case ISD::SETULT: case ISD::SETUGT:
    Cmp = RISCC::SLTU; Br = RISCC::BNEZ; break;
  case ISD::SETUGE: case ISD::SETULE:
    Cmp = RISCC::SLTU; Br = RISCC::BEQZ; break;
  default: llvm_unreachable("unsupported integer condition");
  }
  BuildMI(MBB, I, DL, TII.get(Cmp), RISCC::R0).addReg(LHS).addReg(RHS);
  BuildMI(MBB, I, DL, TII.get(Br)).addMBB(Target);
}

MachineBasicBlock *RISCCTargetLowering::EmitInstrWithCustomInserter(
    MachineInstr &MI, MachineBasicBlock *MBB) const {
  const auto &TII = *STI.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  if (MI.getOpcode() == RISCC::PseudoSHL ||
      MI.getOpcode() == RISCC::PseudoSRL ||
      MI.getOpcode() == RISCC::PseudoSRA) {
    MachineFunction *MF = MBB->getParent();
    const BasicBlock *BB = MBB->getBasicBlock();
    auto InsertAt = std::next(MBB->getIterator());
    MachineBasicBlock *LoopBB = MF->CreateMachineBasicBlock(BB);
    MachineBasicBlock *RemBB = MF->CreateMachineBasicBlock(BB);
    MF->insert(InsertAt, LoopBB);
    MF->insert(InsertAt, RemBB);
    RemBB->splice(RemBB->begin(), MBB, std::next(MI.getIterator()), MBB->end());
    RemBB->transferSuccessorsAndUpdatePHIs(MBB);
    MBB->addSuccessor(LoopBB);
    MBB->addSuccessor(RemBB);
    LoopBB->addSuccessor(LoopBB);
    LoopBB->addSuccessor(RemBB);

    MachineRegisterInfo &MRI = MF->getRegInfo();
    Register ShiftPhi = MRI.createVirtualRegister(&RISCC::GPRRegClass);
    Register ShiftNext = MRI.createVirtualRegister(&RISCC::GPRRegClass);
    Register AmtPhi = MRI.createVirtualRegister(&RISCC::GPRRegClass);
    Register AmtNext = MRI.createVirtualRegister(&RISCC::GPRRegClass);
    Register Dst = MI.getOperand(0).getReg();
    Register Src = MI.getOperand(1).getReg();
    Register Amt = MI.getOperand(2).getReg();

    BuildMI(*MBB, MI, DL, TII.get(RISCC::MOV), RISCC::R0).addReg(Amt);
    BuildMI(*MBB, MI, DL, TII.get(RISCC::BEQZ)).addMBB(RemBB);

    BuildMI(*LoopBB, LoopBB->end(), DL, TII.get(TargetOpcode::PHI), ShiftPhi)
        .addReg(Src).addMBB(MBB).addReg(ShiftNext).addMBB(LoopBB);
    BuildMI(*LoopBB, LoopBB->end(), DL, TII.get(TargetOpcode::PHI), AmtPhi)
        .addReg(Amt).addMBB(MBB).addReg(AmtNext).addMBB(LoopBB);
    unsigned RealOpc = MI.getOpcode() == RISCC::PseudoSHL ? RISCC::SHLI
                       : MI.getOpcode() == RISCC::PseudoSRL ? RISCC::SHRI
                                                            : RISCC::SARI;
    BuildMI(*LoopBB, LoopBB->end(), DL, TII.get(RealOpc), ShiftNext)
        .addReg(ShiftPhi).addImm(1);
    BuildMI(*LoopBB, LoopBB->end(), DL, TII.get(RISCC::ADDI), AmtNext)
        .addReg(AmtPhi).addImm(-1);
    BuildMI(*LoopBB, LoopBB->end(), DL, TII.get(RISCC::MOV), RISCC::R0)
        .addReg(AmtNext);
    BuildMI(*LoopBB, LoopBB->end(), DL, TII.get(RISCC::BNEZ)).addMBB(LoopBB);
    BuildMI(*RemBB, RemBB->begin(), DL, TII.get(TargetOpcode::PHI), Dst)
        .addReg(Src).addMBB(MBB).addReg(ShiftNext).addMBB(LoopBB);
    MI.eraseFromParent();
    return RemBB;
  }
  if (MI.getOpcode() == RISCC::PseudoBRCC) {
    emitComparisonBranch(*MBB, MI, DL, TII, MI.getOperand(0).getReg(),
                         MI.getOperand(1).getReg(),
                         ISD::CondCode(MI.getOperand(2).getImm()),
                         MI.getOperand(3).getMBB());
    MI.eraseFromParent();
    return MBB;
  }

  assert((MI.getOpcode() == RISCC::PseudoSETCC ||
          MI.getOpcode() == RISCC::PseudoSELECTCC) &&
         "unexpected custom inserter opcode");
  MachineFunction *MF = MBB->getParent();
  const BasicBlock *BB = MBB->getBasicBlock();
  auto InsertAt = std::next(MBB->getIterator());
  MachineBasicBlock *TrueBB = MF->CreateMachineBasicBlock(BB);
  MachineBasicBlock *SinkBB = MF->CreateMachineBasicBlock(BB);
  MF->insert(InsertAt, TrueBB);
  MF->insert(InsertAt, SinkBB);
  SinkBB->splice(SinkBB->begin(), MBB, std::next(MI.getIterator()), MBB->end());
  SinkBB->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(TrueBB);
  MBB->addSuccessor(SinkBB);
  TrueBB->addSuccessor(SinkBB);

  Register Dst = MI.getOperand(0).getReg();
  Register LHS = MI.getOperand(1).getReg(), RHS = MI.getOperand(2).getReg();
  unsigned CCOp = MI.getOpcode() == RISCC::PseudoSETCC ? 3 : 5;

  Register FalseV, TrueV;
  if (MI.getOpcode() == RISCC::PseudoSETCC) {
    FalseV = MF->getRegInfo().createVirtualRegister(&RISCC::GPRRegClass);
    TrueV = MF->getRegInfo().createVirtualRegister(&RISCC::GPRRegClass);
    // This must precede the compare and terminators in the original block.
    BuildMI(*MBB, MI, DL, TII.get(RISCC::LDI), FalseV).addImm(0);
    BuildMI(*TrueBB, TrueBB->end(), DL, TII.get(RISCC::LDI), TrueV).addImm(1);
  } else {
    TrueV = MI.getOperand(3).getReg();
    FalseV = MI.getOperand(4).getReg();
  }
  emitComparisonBranch(*MBB, MI, DL, TII, LHS, RHS,
                       ISD::CondCode(MI.getOperand(CCOp).getImm()), TrueBB);
  BuildMI(*MBB, MI, DL, TII.get(RISCC::JMP8)).addMBB(SinkBB);
  BuildMI(*SinkBB, SinkBB->begin(), DL, TII.get(TargetOpcode::PHI), Dst)
      .addReg(FalseV).addMBB(MBB).addReg(TrueV).addMBB(TrueBB);
  MI.eraseFromParent();
  return SinkBB;
}

TargetLowering::ConstraintType
RISCCTargetLowering::getConstraintType(StringRef C) const {
  if (C == "r") return C_RegisterClass;
  return TargetLowering::getConstraintType(C);
}

std::pair<unsigned, const TargetRegisterClass *>
RISCCTargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef C, MVT VT) const {
  if (C == "r" && VT.isInteger()) return {0, &RISCC::GPRRegClass};
  return TargetLowering::getRegForInlineAsmConstraint(TRI, C, VT);
}
