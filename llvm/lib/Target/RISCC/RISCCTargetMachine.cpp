//===-- RISCCTargetMachine.cpp - RISCC TargetMachine ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCTargetMachine.h"
#include "MCTargetDesc/RISCCMCAsmInfo.h"
#include "RISCC.h"
#include "RISCCCallPolicy.h"
#include "RISCCConstantIslandPass.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCMachineOptimize.h"
#include "RISCCPrepare.h"
#include "RISCCSRegAllocator.h"
#include "TargetInfo/RISCCTargetInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/Transforms/Scalar.h"
#include <cmath>

using namespace llvm;

static cl::opt<bool> EnableEarlyCleanup(
    "riscc-early-cleanup", cl::Hidden, cl::init(true),
    cl::desc("Remove dead instructions before RISC-C frame planning"));

bool llvm::enableRISCCEarlyCleanup() { return EnableEarlyCleanup; }

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCCTarget() {
  RegisterTargetMachine<RISCCTargetMachine> X(getTheRISCCTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeRISCCAsmPrinterPass(PR);
  initializeRISCCConstantIslandLegacyPass(PR);
  initializeRISCCDAGToDAGISelLegacyPass(PR);
  initializeRISCCSRegAllocatorLegacyPass(PR);
  initializeRISCCMachineOptimizeLegacyPass(PR);
  initializeRISCCPostRAOptimizeLegacyPass(PR);
  initializeRISCCReturnOptimizeLegacyPass(PR);
  initializeRISCCPrepareLegacyPass(PR);
  initializeRISCCCallPolicyLegacyPass(PR);
}

static std::string computeRISCCDataLayout(const Triple &TT, StringRef FS) {
  SubtargetFeatures Features(FS);
  bool IsRC32 = false;
  for (StringRef Feature : Features.getFeatures()) {
    if (Feature == "+rc32")
      IsRC32 = true;
    else if (Feature == "-rc32")
      IsRC32 = false;
  }
  if (IsRC32)
    return "e-m:e-p:32:32-i8:8-i16:16-i32:32-i64:32-"
           "f32:32-f64:32-a:8:32-n8:16:32-S32";
  return TT.computeDataLayout();
}

RISCCTargetMachine::RISCCTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool)
    : CodeGenTargetMachineImpl(T, computeRISCCDataLayout(TT, FS), TT, CPU, FS,
                               Options, RM.value_or(Reloc::Static),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  if (getRelocationModel() != Reloc::Static)
    reportFatalUsageError("RISC-C supports only static relocation");
  if (getCodeModel() != CodeModel::Small)
    reportFatalUsageError("RISC-C supports only the small code model");
  initAsmInfo();
  auto *MAI =
      static_cast<RISCCMCAsmInfo *>(const_cast<MCAsmInfo *>(AsmInfo.get()));
  MAI->setRC32(Subtarget.isRC32());
  setMachineOutliner(true);
}

RISCCTargetMachine::~RISCCTargetMachine() = default;

const RISCCSubtarget *
RISCCTargetMachine::getSubtargetImpl(const Function &F) const {
  StringRef CPU = getTargetCPU();
  StringRef FS = getTargetFeatureString();
  if (Attribute Attr = F.getFnAttribute("target-cpu"); Attr.isValid())
    CPU = Attr.getValueAsString();
  if (Attribute Attr = F.getFnAttribute("target-features"); Attr.isValid())
    FS = Attr.getValueAsString();
  if (CPU == getTargetCPU() && FS == getTargetFeatureString())
    return &Subtarget;

  auto &ST = SubtargetMap[(CPU + "|" + FS).str()];
  if (!ST) {
    ST = std::make_unique<RISCCSubtarget>(getTargetTriple(), CPU.str(),
                                          FS.str(), *this);
    // Data layout and ELF flags describe one ABI for the whole module.
    if (ST->isRC32() != Subtarget.isRC32() ||
        ST->isNano() != Subtarget.isNano())
      report_fatal_error(
          "RISC-C function target attributes cannot change the ABI");
  }
  return ST.get();
}

MachineFunctionInfo *RISCCTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return RISCCMachineFunctionInfo::create(Allocator, F, STI);
}

namespace {
static cl::opt<bool> EnableNarrowCastCost(
    "riscc-narrow-cast-cost", cl::Hidden, cl::init(true),
    cl::desc(
        "Cost narrow RISC-C extensions using compact instruction sequences"));
static cl::opt<bool> EnableShiftCallCost(
    "riscc-shift-call-cost", cl::Hidden, cl::init(true),
    cl::desc("Charge native RISC-C shift helpers as calls in IR cost queries"));
static cl::opt<bool> EnableArithmeticCallCost(
    "riscc-arithmetic-call-cost", cl::Hidden, cl::init(true),
    cl::desc(
        "Charge native RISC-C arithmetic helpers as calls in IR cost queries"));
static cl::opt<bool> EnablePipelineCost(
    "riscc-pipeline-costs", cl::Hidden, cl::init(true),
    cl::desc("Use RISC-C scheduling latencies for native multiply and loads"));

class RISCCTTIImpl final : public BasicTTIImpl {
  using TTI = TargetTransformInfo;
  bool OptimizeForSize;
  unsigned NativeBits;
  const RISCCSubtarget *ST;
  bool UseShiftHelper;

  InstructionCost getNativeExecutionCost(unsigned Opcode,
                                         TTI::TargetCostKind CostKind) const {
    const RISCCInstrInfo *TII = ST->getInstrInfo();
    const MCSchedModel &Model = ST->getSchedModel();
    const MCSchedClassDesc &Desc = *Model.getSchedClassDesc(
        TII->get(TII->getNativeOpcode(Opcode)).getSchedClass());
    if (CostKind == TTI::TCK_RecipThroughput)
      return static_cast<int64_t>(
          std::ceil(MCSchedModel::getReciprocalThroughput(*ST, Desc)));
    return MCSchedModel::computeInstrLatency(*ST, Desc);
  }

  InstructionCost getBinaryHelperCost(Type *Ty,
                                      TTI::TargetCostKind CostKind) const {
    // Count call-site halfwords, not the shared runtime body: RC16 uses JALL
    // (2) or address materialization plus a call (3); RC32 uses LDPC, JALR and
    // a literal (4). IR costs cannot assume the linker can shorten the call.
    if (CostKind == TTI::TCK_CodeSize)
      return ST->isRC32() ? 4 : (ST->hasLongJall() ? 2 : 3);
    SmallVector<Type *, 2> ArgTys{Ty, Ty};
    return getCallInstrCost(nullptr, Ty, ArgTys, CostKind);
  }

public:
  RISCCTTIImpl(const RISCCTargetMachine *TM, const Function &F)
      : BasicTTIImpl(TM, F), OptimizeForSize(F.hasOptSize()),
        NativeBits(F.getDataLayout().getPointerSizeInBits()),
        ST(TM->getSubtargetImpl(F)),
        UseShiftHelper(ST->isRC32() ||
                       (TM->getOptLevel() != CodeGenOptLevel::None &&
                        !F.hasOptNone() && !F.hasOptSize()) ||
                       (!ST->hasWideShift() && F.hasMinSize())) {}

  unsigned getNumberOfRegisters(unsigned ClassID) const override {
    // The compact ALU bank has eight registers, but R7 is the stack pointer.
    // S registers cache spills; they cannot serve as arithmetic operands.
    return ClassID == 0 ? 7 : 0;
  }

  TypeSize
  getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const override {
    return TypeSize::getFixed(K == TargetTransformInfo::RGK_Scalar ? NativeBits
                                                                   : 0);
  }

  InstructionCost
  getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                   TTI::CastContextHint CCH, TTI::TargetCostKind CostKind,
                   const Instruction *I = nullptr) const override {
    InstructionCost Cost =
        BasicTTIImpl::getCastInstrCost(Opcode, Dst, Src, CCH, CostKind, I);
    // Preserve free extensions, including those folded into a load or
    // supplied by the ABI. Register byte extension uses ANDI/XORI/ADDI;
    // a Full halfword extension uses four eight-bit shifts. Counting each
    // as a single instruction understates duplication and inlining costs.
    if (!EnableNarrowCastCost || !OptimizeForSize || ST->isNano() ||
        Cost == 0 || Opcode != Instruction::SExt || !Dst->isIntegerTy() ||
        Dst->getIntegerBitWidth() > NativeBits)
      return Cost;
    if (Src->isIntegerTy(8))
      return 3;
    if (Src->isIntegerTy(16) && ST->hasWideShift())
      return 4;
    return Cost;
  }

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = {},
      const Instruction *CxtI = nullptr) const override {
    // A native multiply occupies Execute, unlike a one-cycle ALU operation.
    // Keep code size in instruction units and let constants retain their
    // separate folding/strength-reduction costs. Query the scheduling model
    // rather than duplicating its latency or resource occupancy here.
    if (EnablePipelineCost && ST->getSchedModel().hasInstrSchedModel() &&
        ST->hasMul() && Opcode == Instruction::Mul &&
        Ty->isIntegerTy(NativeBits) && CostKind != TTI::TCK_CodeSize &&
        !Op1Info.isConstant() && !Op2Info.isConstant() &&
        llvm::none_of(Args, [](const Value *V) { return isa<Constant>(V); }))
      return getNativeExecutionCost(RISCC::MUL, CostKind);
    if (EnableArithmeticCallCost && Ty->isIntegerTy(NativeBits) &&
        Args.size() == 2 && !isa<Constant>(Args[0]) &&
        !isa<Constant>(Args[1])) {
      // Match the native-width operation actions. Constant multiplication
      // and division can become shifts/adds or reciprocal multiplication;
      // leave those to the existing constant and legalization costing.
      bool UsesHelper =
          Opcode == Instruction::SDiv || Opcode == Instruction::SRem;
      UsesHelper |= !ST->hasDivu() && (Opcode == Instruction::UDiv ||
                                       Opcode == Instruction::URem);
      UsesHelper |= !ST->hasMul() && Opcode == Instruction::Mul;
      if (UsesHelper)
        return getBinaryHelperCost(Ty, CostKind);
    }
    if (Ty->isIntegerTy(NativeBits) &&
        (Opcode == Instruction::Shl || Opcode == Instruction::LShr ||
         Opcode == Instruction::AShr) &&
        Args.size() == 2) {
      if (const auto *Amount = dyn_cast<ConstantInt>(Args[1]);
          Amount && Amount->getValue().ult(NativeBits)) {
        unsigned Shift = Amount->getZExtValue();
        // Match lowerShift: Full emits chunks of up to eight bits, while
        // Min, Sys and Nano require one instruction per bit. A non-Nano
        // sign mask can instead use a zero, comparison and negation.
        if (!ST->isNano() && Opcode == Instruction::AShr &&
            Shift == NativeBits - 1 &&
            (!OptimizeForSize || !ST->hasWideShift()))
          return 3;
        return divideCeil(Shift, ST->hasWideShift() ? 8u : 1u);
      }
      // Match lowerShift's helper choice so speculation and duplication pay
      // for a call. Constant shifts and bounded (0..3) inline loops retain
      // their existing costs.
      if (EnableShiftCallCost && UseShiftHelper && !isa<Constant>(Args[1]) &&
          !computeKnownBits(Args[1], DL, nullptr, CxtI).getMaxValue().ule(3)) {
        return getBinaryHelperCost(Ty, CostKind);
      }
    }
    return BasicTTIImpl::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                                Op2Info, Args, CxtI);
  }

  InstructionCost getMemoryOpCost(
      unsigned Opcode, Type *Ty, Align Alignment, unsigned AddressSpace,
      TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo OpInfo = {TTI::OK_AnyValue, TTI::OP_None},
      const Instruction *I = nullptr) const override {
    // Aligned scalar integer loads fit a single native, halfword or byte
    // instruction. Their dependent use has the machine model's load latency;
    // their throughput and size still cost one instruction. Do not apply a
    // single-load estimate to wider, unaligned or unsupported types, or Nano.
    if (EnablePipelineCost && ST->getSchedModel().hasInstrSchedModel() &&
        Opcode == Instruction::Load && CostKind == TTI::TCK_Latency &&
        Ty->isIntegerTy() &&
        (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) ||
         Ty->isIntegerTy(NativeBits)) &&
        Alignment.value() >= Ty->getIntegerBitWidth() / 8)
      return getNativeExecutionCost(RISCC::LD, CostKind);
    return BasicTTIImpl::getMemoryOpCost(Opcode, Ty, Alignment, AddressSpace,
                                         CostKind, OpInfo, I);
  }

  bool isLSRCostLess(const TargetTransformInfo::LSRCost &A,
                    const TargetTransformInfo::LSRCost &B) const override {
    if (OptimizeForSize)
      return BasicTTIImpl::isLSRCostLess(A, B);
    // Registers remain the limiting resource. Among equally demanding loop
    // forms, prefer fewer instructions before the individual addressing costs.
    return std::tie(A.NumRegs, A.Insns, A.AddRecCost, A.NumIVMuls,
                    A.NumBaseAdds, A.ScaleCost, A.ImmCost, A.SetupCost) <
           std::tie(B.NumRegs, B.Insns, B.AddRecCost, B.NumIVMuls,
                    B.NumBaseAdds, B.ScaleCost, B.ImmCost, B.SetupCost);
  }
};

class RISCCPassConfigImpl final : public TargetPassConfig {
public:
  RISCCPassConfigImpl(RISCCTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {
    substitutePass(&PostRASchedulerID, &PostMachineSchedulerID);
  }
  void addIRPasses() override {
    addPass(createAtomicExpandLegacyPass());
    // Preserve safe narrow recurrences at native width before loop strength
    // reduction rewrites their induction variables and overflow information.
    if (getOptLevel() != CodeGenOptLevel::None)
      addPass(createTypePromotionLegacyPass());
    TargetPassConfig::addIRPasses();
  }
  bool addPreISel() override {
    if (getOptLevel() != CodeGenOptLevel::None) {
      addPass(createGlobalMergePass(TM, 127, false, true));
      addPass(createRISCCPrepareLegacyPass());
      // Reuse related addresses after preparation exposes loop-carried bases.
      addPass(createStraightLineStrengthReducePass());
    }
    addPass(createRISCCCallPolicyLegacyPass());
    return false;
  }
  bool addInstSelector() override {
    addPass(createRISCCISelDag(getTM<RISCCTargetMachine>(), getOptLevel()));
    return false;
  }
  void addPreRegAlloc() override {
    if (getOptLevel() != CodeGenOptLevel::None)
      addPass(createRISCCMachineOptimizeLegacyPass());
  }
  void addPostRewrite() override {
    // Rematerialization can leave unused original definitions. Remove them
    // before planning spill-cache registers and callee-save/frame storage.
    if (getOptLevel() != CodeGenOptLevel::None && enableRISCCEarlyCleanup())
      addPass(&DeadMachineInstructionElimID);
    addPass(createRISCCSRegAllocatorLegacyPass());
  }
  void addPreSched2() override {
    if (getOptLevel() != CodeGenOptLevel::None) {
      addPass(createRISCCPostRAOptimizeLegacyPass());
      if (enableRISCCConstantBranches())
        addPass(&UnreachableMachineBlockElimID);
    }
  }
  void addPreEmitPass() override {
    if (getOptLevel() >= CodeGenOptLevel::Default)
      addPass(createMachineCopyPropagationPass(true));
    // Scheduling's anti-dependency renaming can free an epilogue target GPR
    // that was still clobbered by a large SP adjustment before scheduling.
    if (getOptLevel() != CodeGenOptLevel::None)
      addPass(createRISCCReturnOptimizeLegacyPass());
    addPass(&BranchRelaxationPassID);
  }
  void addPreEmitPass2() override { addPass(createRISCCConstantIslandPass()); }
};
} // namespace

TargetTransformInfo
RISCCTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<RISCCTTIImpl>(this, F));
}

TargetPassConfig *RISCCTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new RISCCPassConfigImpl(*this, PM);
}
