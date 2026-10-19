//===-- RISCCMachineOptimize.cpp - Machine SSA optimizations --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Clean up machine SSA before allocation and redundant physical-register
// computations afterwards.
//===----------------------------------------------------------------------===//
#include "RISCCMachineOptimize.h"
#include "RISCCInstrInfo.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCMatInt.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/IR/Constants.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
#define DEBUG_TYPE "riscc-machine-optimize"

STATISTIC(NumConstantBranches, "Number of constant RISC-C branches folded");

namespace {
static cl::opt<bool> EnableAffineValues(
    "riscc-affine-values", cl::Hidden, cl::init(true),
    cl::desc("Reuse related physical-register values on RISC-C"));
static cl::opt<bool> EnableArgumentWidths(
    "riscc-argument-widths", cl::Hidden, cl::init(true),
    cl::desc("Retain RISC-C argument extension facts through machine SSA"));
static cl::opt<bool> EnableJoinValues(
    "riscc-join-values", cl::Hidden, cl::init(true),
    cl::desc("Meet physical-register values at forward CFG joins on RISC-C"));
static cl::opt<bool> EnableBranchEquality(
    "riscc-branch-equality", cl::Hidden, cl::init(true),
    cl::desc(
        "Infer zero/equality values on forward RISC-C branch edges for size"));
static cl::opt<bool> EnableConstantBranches(
    "riscc-constant-branches", cl::Hidden, cl::init(true),
    cl::desc("Fold RISC-C branches with a proven constant condition"));
static cl::opt<cl::boolOrDefault> EnableLocalCopies(
    "riscc-local-copies", cl::Hidden, cl::init(cl::boolOrDefault::BOU_UNSET),
    cl::desc("Coalesce physical-register copies using bounded local value "
             "analysis (default: size optimization)"));
static cl::opt<bool> EnableStackValues(
    "riscc-stack-values", cl::Hidden, cl::init(false),
    cl::desc("Experimentally reuse available fixed-stack reloads on RISC-C"));
static cl::opt<bool>
    EnableInvariantCopies("riscc-invariant-copies", cl::Hidden, cl::init(false),
                          cl::desc("Experimentally hoist invariant physical "
                                   "copy/immediate computations on RISC-C"));
static cl::opt<bool> EnableLoopBranchRegister(
    "riscc-loop-branch-register", cl::Hidden, cl::init(true),
    cl::desc("Keep a single-block loop's carried branch value in R0"));
static cl::opt<bool> EnableLiteralValues(
    "riscc-literal-values", cl::Hidden, cl::init(true),
    cl::desc("Reuse physical-register values of immutable literals on RISC-C"));
static cl::opt<bool> EnableAffineLiterals(
    "riscc-affine-literals", cl::Hidden, cl::init(true),
    cl::desc("Form RISC-C literals from nearby available values for size"));
static cl::opt<bool> EnablePreservedValues(
    "riscc-preserved-values", cl::Hidden, cl::init(true),
    cl::desc("Retain physical value relations in call-preserved registers"));
static cl::opt<bool> EnableBranchCopyPaths(
    "riscc-branch-copy-paths", cl::Hidden, cl::init(true),
    cl::desc(
        "Coalesce a loop branch copy with its successor's register handoff"));
static cl::opt<bool> EnableConstantSynthesis(
    "riscc-synthesize-constants", cl::Hidden, cl::init(true),
    cl::desc("Synthesize unshared RC32 literals with compact instructions"));
static cl::opt<cl::boolOrDefault> EnableUnusedUpperBits(
    "riscc-unused-upper-bits", cl::Hidden,
    cl::init(cl::boolOrDefault::BOU_UNSET),
    cl::desc("Remove RISC-C normalizations whose upper bits are unused "
             "(default: size optimization)"));
static cl::opt<bool>
    EnableFoldAddressOffsets("riscc-fold-address-offsets", cl::Hidden,
                             cl::init(false),
                             cl::desc("Fold RISC-C address adjustments through "
                                      "arithmetic into memory offsets"));
static cl::opt<bool> EnableLateSignMasks(
    "riscc-late-sign-mask", cl::Hidden, cl::init(true),
    cl::desc(
        "Use dead physical registers to shorten RISC-C sign masks for size"));
static cl::opt<bool> EnableSignBranchShifts(
    "riscc-sign-branch-shifts", cl::Hidden, cl::init(true),
    cl::desc("Remove RISC-C arithmetic shifts used only for a sign branch"));

static bool enableLocalCopies(const MachineFunction &MF) {
  // Local instruction/latency savings do not predict whole-function fetch
  // and branch-layout effects. Use reassignment for size by default; speed
  // experiments still apply the planner's latency and live-out cost checks.
  return EnableLocalCopies == cl::boolOrDefault::BOU_TRUE ||
         (EnableLocalCopies == cl::boolOrDefault::BOU_UNSET &&
          MF.getFunction().hasOptSize());
}

// A value copied to R0 for a branch may immediately enter another block that
// copies it again to a dead register. Produce it in R0, and move that existing
// second copy before its uses. In a two-block loop the old source is redefined
// on the backedge; only an exit may need its original physical assignment.
// An exit repair replaces the removed copy statically, and runs once rather
// than on every loop trip. No memory operation moves or changes its address.
static bool coalesceLoopBranchCopyPaths(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  const unsigned MoveOp = TII.getNativeOpcode(RISCC::MOV);
  auto IsBarrier = [](const MachineInstr &MI) {
    return MI.isDebugInstr() || MI.isCall() || MI.isInlineAsm() ||
           MI.isBundled() || MI.isCFIInstruction() ||
           MI.hasUnmodeledSideEffects() ||
           MI.getFlag(MachineInstr::FrameSetup) ||
           MI.getFlag(MachineInstr::FrameDestroy);
  };
  auto CanRename = [&](const MachineInstr &MI, const MachineOperand &MO,
                       Register To) {
    if (!MO.isRenamable() || MO.isImplicit() || MO.getSubReg() || MO.isUndef())
      return false;
    const auto *RC = MI.getRegClassConstraint(MI.getOperandNo(&MO), &TII, &TRI);
    return !RC || RC->contains(To);
  };
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.succ_size() != 2 || MBB.isEHPad() || MBB.hasAddressTaken())
      continue;
    auto Branch = MBB.getFirstTerminator();
    if (Branch == MBB.end() || Branch == MBB.begin() ||
        !Branch->isConditionalBranch() ||
        !Branch->readsRegister(RISCC::R0, &TRI))
      continue;
    auto Copy = std::prev(Branch);
    if (Copy->getOpcode() != MoveOp || Copy->getNumOperands() != 2 ||
        Copy->getOperand(0).getReg() != RISCC::R0 ||
        Copy->getOperand(1).isUndef() || Copy->getOperand(1).getSubReg() ||
        IsBarrier(*Copy))
      continue;
    Register Src = Copy->getOperand(1).getReg();
    if (Src == RISCC::R0 || Src == RISCC::R7 ||
        !STI.getGPRClass()->contains(Src) || MBB.isLiveIn(Src))
      continue;
    MachineBasicBlock *Body = nullptr;
    bool Safe = true;
    for (MachineBasicBlock *Succ : MBB.successors())
      if (Succ->isLiveIn(Src)) {
        if (Body)
          Safe = false;
        Body = Succ;
      }
    if (!Safe || !Body || Body == &MBB || Body->pred_size() != 1 ||
        Body->succ_size() != 2 || !Body->isSuccessor(&MBB) || Body->isEHPad() ||
        Body->hasAddressTaken())
      continue;
    MachineBasicBlock *Exit = nullptr;
    for (MachineBasicBlock *Succ : Body->successors())
      if (Succ != &MBB)
        Exit = Succ;
    if (!Exit || Exit == Body || Exit->isEHPad() || Exit->hasAddressTaken())
      continue;
    bool Repair = Exit->isLiveIn(Src);
    // An inserted exit block must not intercept an unrelated fallthrough.
    if (Repair && !Body->isLayoutSuccessor(Exit))
      continue;

    MachineInstr *Def = nullptr;
    for (auto I = Copy; I != MBB.begin();) {
      MachineInstr &MI = *--I;
      if (IsBarrier(MI) || MI.isTerminator() ||
          MI.modifiesRegister(RISCC::R0, &TRI))
        break;
      if (MI.modifiesRegister(Src, &TRI)) {
        if (MI.getNumDefs() == 1 && MI.getOperand(0).isReg() &&
            MI.getOperand(0).getReg() == Src && !MI.getOperand(0).isTied() &&
            !MI.getOperand(0).isEarlyClobber() &&
            CanRename(MI, MI.getOperand(0), RISCC::R0))
          Def = &MI;
        break;
      }
      if (MI.readsRegister(RISCC::R0, &TRI))
        break;
    }
    if (!Def)
      continue;
    for (MachineInstr &MI :
         make_range(std::next(Def->getIterator()), Copy->getIterator()))
      for (const MachineOperand &MO : MI.operands())
        if (MO.isReg() && MO.getReg() == Src && !CanRename(MI, MO, RISCC::R0))
          Safe = false;
    // Only the conditional branch may observe the new R0 result here.
    for (MachineInstr &MI : make_range(Branch, MBB.end()))
      if (MI.readsRegister(Src, &TRI) || MI.modifiesRegister(Src, &TRI) ||
          MI.modifiesRegister(RISCC::R0, &TRI) || IsBarrier(MI))
        Safe = false;
    if (!Safe)
      continue;

    MachineInstr *Handoff = nullptr;
    Register Dst;
    for (MachineInstr &MI : *Body) {
      if (MI.getOpcode() != MoveOp || MI.getNumOperands() != 2 ||
          MI.getOperand(1).getReg() != Src || MI.getOperand(1).isUndef() ||
          MI.getOperand(1).getSubReg() || IsBarrier(MI))
        continue;
      Register R = MI.getOperand(0).getReg();
      if (R != Src && R != RISCC::R0 && R != RISCC::R7 &&
          STI.getGPRClass()->contains(R) && !Body->isLiveIn(R)) {
        Handoff = &MI;
        Dst = R;
        break;
      }
    }
    if (!Handoff)
      continue;
    bool SawHandoff = false;
    for (MachineInstr &MI : *Body) {
      if (&MI == Handoff) {
        SawHandoff = true;
        continue;
      }
      if (IsBarrier(MI) || MI.modifiesRegister(Src, &TRI) ||
          MI.modifiesRegister(Dst, &TRI) ||
          (!SawHandoff && MI.readsRegister(Dst, &TRI)))
        Safe = false;
      for (const MachineOperand &MO : MI.operands())
        if (MO.isReg() && MO.getReg() == Src && !CanRename(MI, MO, Dst))
          Safe = false;
    }
    if (!Safe)
      continue;

    Def->getOperand(0).setReg(RISCC::R0);
    Def->getOperand(0).setIsRenamable(true);
    for (MachineInstr &MI :
         make_range(std::next(Def->getIterator()), Copy->getIterator()))
      for (MachineOperand &MO : MI.operands())
        if (MO.isReg() && MO.getReg() == Src) {
          MO.setReg(RISCC::R0);
          MO.setIsRenamable(true);
        }
    Copy->eraseFromParent();
    for (MachineInstr &MI : *Body)
      if (&MI != Handoff)
        for (MachineOperand &MO : MI.operands())
          if (MO.isReg() && MO.getReg() == Src) {
            MO.setReg(Dst);
            MO.setIsRenamable(true);
          }
    Handoff->getOperand(1).setReg(RISCC::R0);
    Handoff->getOperand(1).setIsRenamable(true);
    Body->splice(Body->begin(), Body, Handoff->getIterator());
    Body->removeLiveIn(Src);
    Body->addLiveIn(RISCC::R0);
    Body->sortUniqueLiveIns();
    if (Repair) {
      MachineBasicBlock *Fix = MF.CreateMachineBasicBlock();
      MF.insert(std::next(Body->getIterator()), Fix);
      for (const auto &LiveIn : Exit->liveins())
        if (LiveIn.PhysReg != Src.asMCReg())
          Fix->addLiveIn(LiveIn);
      Fix->addLiveIn(Dst);
      Fix->sortUniqueLiveIns();
      Fix->addSuccessor(Exit);
      BuildMI(*Fix, Fix->end(), Handoff->getDebugLoc(), TII.get(MoveOp), Src)
          .addReg(Dst);
      Body->ReplaceUsesOfBlockWith(Exit, Fix);
      recomputeLivenessFlags(*Fix);
    }
    recomputeLivenessFlags(MBB);
    recomputeLivenessFlags(*Body);
    Changed = true;
  }
  return Changed;
}

// A loop may carry its branch value in another GPR while using R0 for short
// temporaries, then copy that value to R0 on every backedge. Swap the two
// allocations inside the loop. One entry copy replaces the repeated copy;
// the old carrier must be dead at the exit, so no exit repair is needed.
// This is a bijective rename, not an extension of a live range: pressure and
// instruction count do not increase, even for a one-iteration loop.
// The carrier must already be written in the loop, so the swap cannot
// introduce a new callee-saved clobber after prologue insertion.
static bool keepLoopBranchValueInR0(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  const unsigned MoveOp = TII.getNativeOpcode(RISCC::MOV);
  bool Changed = false;
  for (MachineBasicBlock &Loop : MF) {
    if (Loop.pred_size() < 2 || Loop.succ_size() != 2 ||
        !Loop.isSuccessor(&Loop) || Loop.isLiveIn(RISCC::R0) ||
        Loop.isEHPad() || Loop.hasAddressTaken())
      continue;
    MachineBasicBlock *Entry = nullptr, *Exit = nullptr;
    SmallVector<MachineBasicBlock *, 4> OutsidePreds;
    for (MachineBasicBlock *Pred : Loop.predecessors())
      if (Pred != &Loop)
        OutsidePreds.push_back(Pred);
    for (MachineBasicBlock *Succ : Loop.successors())
      if (Succ != &Loop)
        Exit = Succ;
    if (OutsidePreds.empty() || !Exit)
      continue;
    if (OutsidePreds.size() == 1 && OutsidePreds[0]->succ_size() == 1)
      Entry = OutsidePreds[0];
    auto Branch = Loop.getFirstTerminator();
    if (Branch == Loop.end() || Branch == Loop.begin() ||
        !Branch->isConditionalBranch() ||
        !Branch->readsRegister(RISCC::R0, &TRI))
      continue;
    auto Copy = Branch;
    do {
      --Copy;
    } while (Copy != Loop.begin() && Copy->isDebugInstr());
    if ((!Copy->isCopy() && Copy->getOpcode() != MoveOp) ||
        Copy->getNumOperands() != 2 ||
        Copy->getOperand(0).getReg() != RISCC::R0 ||
        Copy->getOperand(1).isUndef() || Copy->getOperand(1).getSubReg())
      continue;
    Register Carrier = Copy->getOperand(1).getReg();
    if (!Carrier.isPhysical() || Carrier == RISCC::R0 || Carrier == RISCC::R7 ||
        !STI.getGPRClass()->contains(Carrier) || !Loop.isLiveIn(Carrier) ||
        Exit->isLiveIn(Carrier))
      continue;
    if (Entry &&
        llvm::any_of(make_range(Entry->getFirstTerminator(), Entry->end()),
                     [&](const MachineInstr &MI) {
                       return MI.readsRegister(RISCC::R0, &TRI) ||
                              MI.modifiesRegister(RISCC::R0, &TRI);
                     }))
      Entry = nullptr;
    bool Safe = llvm::all_of(OutsidePreds, [](const MachineBasicBlock *Pred) {
      return !Pred->isEHPad() &&
             llvm::none_of(Pred->terminators(), [](const MachineInstr &MI) {
               return MI.isIndirectBranch() || MI.isInlineAsm();
             });
    });
    bool CarrierDefined = false;
    for (MachineInstr &MI : Loop) {
      if (&MI == &*Copy)
        continue;
      if (MI.isCall() || MI.isInlineAsm() || MI.isBundled() ||
          MI.hasUnmodeledSideEffects() || MI.isCFIInstruction() ||
          MI.getFlag(MachineInstr::FrameSetup) ||
          MI.getFlag(MachineInstr::FrameDestroy)) {
        Safe = false;
        break;
      }
      for (MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || (MO.getReg() != Carrier && MO.getReg() != RISCC::R0))
          continue;
        CarrierDefined |= MO.getReg() == Carrier && MO.isDef();
        // Only the final conditional branch has a fixed R0 use. Any other
        // fixed/implicit operand would make this allocation swap illegal.
        if (&MI == &*Branch && MO.getReg() == RISCC::R0 && MO.isImplicit() &&
            MO.isUse())
          continue;
        if (!MO.isRenamable() || MO.isImplicit() || MO.getSubReg() ||
            MI.isTerminator() || MI.isDebugInstr())
          Safe = false;
        if (const TargetRegisterClass *RC =
                MI.getRegClassConstraint(MI.getOperandNo(&MO), &TII, &TRI))
          if (!RC->contains(MO.getReg() == Carrier ? Register(RISCC::R0)
                                                   : Carrier))
            Safe = false;
      }
    }
    if (!Safe || !CarrierDefined)
      continue;
    if (!Entry) {
      // A conditional predecessor may itself need R0, and several entries
      // can converge directly on the loop. Give the one-time copy its own
      // preheader rather than inserting it before an incoming condition.
      // It falls through into Loop; only outside edges are redirected.
      Entry = MF.CreateMachineBasicBlock();
      MF.insert(Loop.getIterator(), Entry);
      for (const auto &LiveIn : Loop.liveins())
        Entry->addLiveIn(LiveIn);
      Entry->addSuccessor(&Loop);
      for (MachineBasicBlock *Pred : OutsidePreds)
        Pred->ReplaceUsesOfBlockWith(&Loop, Entry);
    }
    BuildMI(*Entry, Entry->getFirstTerminator(), Copy->getDebugLoc(),
            TII.get(MoveOp), RISCC::R0)
        .addReg(Carrier);
    for (MachineInstr &MI : Loop) {
      if (&MI == &*Copy || &MI == &*Branch)
        continue;
      for (MachineOperand &MO : MI.operands())
        if (MO.isReg()) {
          if (MO.getReg() == Carrier) {
            MO.setReg(RISCC::R0);
            MO.setIsRenamable(true);
          } else if (MO.getReg() == RISCC::R0) {
            MO.setReg(Carrier);
            MO.setIsRenamable(true);
          }
        }
    }
    Copy->eraseFromParent();
    Loop.removeLiveIn(Carrier);
    Loop.addLiveIn(RISCC::R0);
    Loop.sortUniqueLiveIns();
    recomputeLivenessFlags(*Entry);
    recomputeLivenessFlags(Loop);
    Changed = true;
  }
  return Changed;
}

// Scheduling regions end at calls. Delay a cheap add until after the last
// intervening call when its base is already live there, saving a preserved
// register without lengthening another value's lifetime. Stay within one
// block and machine SSA; no physical-register or memory operations move.
static bool delayAddsAcrossCalls(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  if (!MRI.isSSA())
    return false;
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    SmallVector<MachineInstr *> Adds;
    for (MachineInstr &MI : MBB) {
      if ((MI.getOpcode() == RISCC::ADDI || MI.getOpcode() == RISCC::ADDI32) &&
          MI.getNumOperands() == 3 &&
          MI.getOperand(0).getReg().isVirtual() &&
          MI.getOperand(1).getReg().isVirtual())
        Adds.push_back(&MI);
    }
    for (MachineInstr *Add : llvm::reverse(Adds)) {
      Register Dst = Add->getOperand(0).getReg();
      Register Base = Add->getOperand(1).getReg();
      MachineInstr *FirstUse = nullptr;
      if (llvm::any_of(MRI.use_nodbg_instructions(Dst),
                       [&](const MachineInstr &Use) {
                         return Use.getParent() != &MBB || Use.isPHI();
                       }))
        continue;
      MachineInstr *LastCall = nullptr;
      for (auto I = std::next(Add->getIterator()); I != MBB.end(); ++I) {
        if (I->isDebugInstr())
          continue;
        if (I->readsVirtualRegister(Dst)) {
          FirstUse = &*I;
          break;
        }
        if (I->isCall())
          LastCall = &*I;
      }
      if (!FirstUse || !LastCall)
        continue;
      auto AfterCall =
          make_range(std::next(LastCall->getIterator()), MBB.instr_end());
      bool BaseIsLive = llvm::any_of(AfterCall, [&](MachineInstr &Use) {
        return !Use.isDebugInstr() && Use.readsVirtualRegister(Base);
      });
      if (!BaseIsLive)
        continue;
      MRI.markUsesInDebugValueAsUndef(Dst);
      MBB.splice(FirstUse->getIterator(), &MBB, Add->getIterator());
      Changed = true;
    }
  }
  return Changed;
}

static std::optional<APInt> getConstant(const MachineFunction &MF,
                                        const MachineOperand &MO,
                                        unsigned FullWidth) {
  if (!MO.isReg() || MO.isUndef() || MO.getSubReg())
    return std::nullopt;
  Register Reg = MO.getReg();
  const MachineInstr *Def =
      Reg.isVirtual() ? MF.getRegInfo().getVRegDef(Reg) : nullptr;
  if (!Def)
    return std::nullopt;
  switch (Def->getOpcode()) {
  case RISCC::LDI:
  case RISCC::LDI32:
  case RISCC::LDI16:
    if (Def->getOperand(1).isImm())
      return APInt(64, Def->getOperand(1).getImm()).trunc(FullWidth);
    break;
  case RISCC::LUI:
    return APInt(FullWidth, Def->getOperand(1).getImm() << 8);
  case RISCC::LDPC:
    if (Def->getOperand(1).isCPI() && Def->getOperand(1).getOffset() == 0) {
      const auto &Entry =
          MF.getConstantPool()->getConstants()[Def->getOperand(1).getIndex()];
      if (!Entry.isMachineConstantPoolEntry())
        if (const auto *C = dyn_cast<ConstantInt>(Entry.Val.ConstVal))
          if (C->getBitWidth() == FullWidth)
            return C->getValue();
    }
    break;
  default:
    break;
  }
  return std::nullopt;
}

static unsigned getExtensionWidth(unsigned Opcode) {
  switch (Opcode) {
  case RISCC::SEXT8_RC16:
  case RISCC::SEXT8_RC32:
    return 8;
  case RISCC::SEXT16_RC32:
  case RISCC::SEXT16_SHIFT_RC32:
    return 16;
  default:
    return 0;
  }
}

static bool removeRedundantExtensions(MachineFunction &MF) {
  const RISCCSubtarget &STI = MF.getSubtarget<RISCCSubtarget>();
  if (!MF.getRegInfo().isSSA())
    return false;
  const RISCCInstrInfo &TII = *STI.getInstrInfo();
  const unsigned FullWidth = STI.isRC32() ? 32 : 16;
  DenseMap<Register, unsigned> Widths;
  DenseMap<Register, unsigned> UnsignedWidths;
  if (EnableArgumentWidths) {
    // Seed only an intact entry COPY's SSA value. Physical argument registers
    // can be redefined, and a removed or replaced live-in may lose this fact.
    const auto &MRI = MF.getRegInfo();
    const auto &TRI = *STI.getRegisterInfo();
    for (const auto &Ext :
         MF.getInfo<RISCCMachineFunctionInfo>()->getArgumentExtensions()) {
      Register VR = MRI.getLiveInVirtReg(Ext.Reg);
      MachineInstr *Def = VR ? MRI.getVRegDef(VR) : nullptr;
      if (!Def || !Def->isCopy() || Def->isBundled() ||
          Def->getParent() != &MF.front() || Def->getOperand(0).getSubReg() ||
          Def->getOperand(1).getReg() != Ext.Reg ||
          Def->getOperand(1).isUndef() || Def->getOperand(1).getSubReg() ||
          Ext.Bits >= FullWidth ||
          llvm::any_of(make_range(MF.front().instr_begin(), Def->getIterator()),
                       [&](const MachineInstr &MI) {
                         return MI.modifiesRegister(Ext.Reg, &TRI);
                       }))
        continue;
      Widths[VR] = Ext.Bits + !Ext.Signed;
      if (!Ext.Signed)
        UnsignedWidths[VR] = Ext.Bits;
    }
  }
  auto Width = [&](Register Reg) {
    auto I = Widths.find(Reg);
    return I == Widths.end() ? FullWidth : I->second;
  };
  auto UnsignedWidth = [&](Register Reg) {
    auto I = UnsignedWidths.find(Reg);
    return I == UnsignedWidths.end() ? FullWidth : I->second;
  };
  auto OperandWidth = [&](const MachineOperand &MO) {
    return MO.isUndef() || MO.getSubReg() ? FullWidth : Width(MO.getReg());
  };
  auto OperandUnsignedWidth = [&](const MachineOperand &MO) {
    return MO.isUndef() || MO.getSubReg() ? FullWidth
                                          : UnsignedWidth(MO.getReg());
  };
  // Apart from the entry facts, start with unknown values. Each iteration
  // only narrows widths, so cycles without a known defining value remain
  // conservatively unknown.
  bool Progress;
  do {
    Progress = false;
    for (MachineBasicBlock &MBB : MF)
      for (MachineInstr &MI : MBB) {
        if (MI.getNumOperands() == 0 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(0).isDef() || !MI.getOperand(0).getReg().isVirtual())
          continue;
        unsigned Bits = FullWidth;
        unsigned UnsignedBits = FullWidth;
        auto SrcWidth = [&](unsigned I) {
          return OperandWidth(MI.getOperand(I));
        };
        auto ImmWidth = [&](unsigned I) {
          return APInt(64, MI.getOperand(I).getImm())
              .trunc(FullWidth)
              .getSignificantBits();
        };
        auto SrcUnsignedWidth = [&](unsigned I) {
          return OperandUnsignedWidth(MI.getOperand(I));
        };
        auto ImmUnsignedWidth = [&](unsigned I) {
          return APInt(FullWidth, MI.getOperand(I).getImm()).getActiveBits();
        };
        switch (MI.getOpcode()) {
        case RISCC::SEXT8_RC16:
        case RISCC::SEXT8_RC32:
        case RISCC::LDBS:
        case RISCC::LDBS32:
          Bits = 8;
          break;
        case RISCC::SEXT16_RC32:
        case RISCC::SEXT16_SHIFT_RC32:
        case RISCC::LDHS:
          Bits = 16;
          break;
        case RISCC::LDB:
        case RISCC::LDB32:
          Bits = 9;
          UnsignedBits = 8;
          break;
        case RISCC::LDH:
          Bits = 17;
          UnsignedBits = 16;
          break;
        case RISCC::LDI:
        case RISCC::LDI32:
        case RISCC::LDI16:
        case RISCC::LUI:
        case RISCC::LDPC:
          if (auto C = getConstant(MF, MI.getOperand(0), FullWidth)) {
            Bits = C->getSignificantBits();
            UnsignedBits = C->getActiveBits();
          }
          break;
        case RISCC::SLT:
        case RISCC::SLTU:
        case RISCC::SLT32:
        case RISCC::SLTU32:
          Bits = 2; // Either zero or one.
          UnsignedBits = 1;
          break;
        case RISCC::ANDI:
        case RISCC::ANDI32:
          Bits = ImmWidth(2); // The byte mask always clears the sign bit.
          UnsignedBits = std::min(SrcUnsignedWidth(1), ImmUnsignedWidth(2));
          break;
        case RISCC::ORI:
        case RISCC::ORI32:
        case RISCC::XORI:
        case RISCC::XORI32:
          Bits = std::max(SrcWidth(1), ImmWidth(2));
          UnsignedBits = std::max(SrcUnsignedWidth(1), ImmUnsignedWidth(2));
          break;
        case RISCC::AND:
        case RISCC::AND32:
        case RISCC::OR:
        case RISCC::OR32:
        case RISCC::XOR:
        case RISCC::XOR32:
          Bits = std::max(SrcWidth(1), SrcWidth(2));
          UnsignedBits =
              (MI.getOpcode() == RISCC::AND || MI.getOpcode() == RISCC::AND32)
                  ? std::min(SrcUnsignedWidth(1), SrcUnsignedWidth(2))
                  : std::max(SrcUnsignedWidth(1), SrcUnsignedWidth(2));
          if (MI.getOpcode() == RISCC::AND || MI.getOpcode() == RISCC::AND32)
            for (unsigned I : {1u, 2u})
              if (auto C = getConstant(MF, MI.getOperand(I), FullWidth);
                  C && !C->isNegative())
                Bits = std::min(Bits, C->getSignificantBits());
          break;
        case RISCC::ADD:
        case RISCC::ADD32:
        case RISCC::SUB:
        case RISCC::SUB32:
          Bits = std::max(SrcWidth(1), SrcWidth(2)) + 1;
          if (MI.getOpcode() == RISCC::ADD || MI.getOpcode() == RISCC::ADD32)
            UnsignedBits =
                std::max(SrcUnsignedWidth(1), SrcUnsignedWidth(2)) + 1;
          break;
        case RISCC::ADDI:
        case RISCC::ADDI32:
          Bits = std::max(SrcWidth(1), ImmWidth(2)) + 1;
          if (MI.getOperand(2).getImm() >= 0)
            UnsignedBits =
                std::max(SrcUnsignedWidth(1), ImmUnsignedWidth(2)) + 1;
          break;
        case RISCC::SLLI:
        case RISCC::SLLI32:
          Bits = SrcWidth(1) + MI.getOperand(2).getImm();
          UnsignedBits = SrcUnsignedWidth(1) + MI.getOperand(2).getImm();
          break;
        case RISCC::SRAI:
        case RISCC::SRAI32:
          Bits = std::max(1, int(SrcWidth(1)) - int(MI.getOperand(2).getImm()));
          if (SrcUnsignedWidth(1) < FullWidth)
            UnsignedBits = std::max(0, int(SrcUnsignedWidth(1)) -
                                           int(MI.getOperand(2).getImm()));
          break;
        case RISCC::SRLI:
        case RISCC::SRLI32:
          Bits = FullWidth - MI.getOperand(2).getImm() + 1;
          UnsignedBits = std::max(0, int(SrcUnsignedWidth(1)) -
                                         int(MI.getOperand(2).getImm()));
          break;
        case TargetOpcode::COPY:
          Bits = SrcWidth(1);
          UnsignedBits = SrcUnsignedWidth(1);
          break;
        case TargetOpcode::PHI:
          Bits = 1;
          UnsignedBits = 0;
          for (unsigned I = 1; I < MI.getNumOperands(); I += 2) {
            Bits = std::max(Bits, SrcWidth(I));
            UnsignedBits = std::max(UnsignedBits, SrcUnsignedWidth(I));
          }
          break;
        default:
          break;
        }
        Register Dest = MI.getOperand(0).getReg();
        Bits = std::min(Bits, UnsignedBits + 1);
        if (Bits < Width(Dest)) {
          Widths[Dest] = Bits;
          Progress = true;
        }
        if (UnsignedBits < UnsignedWidth(Dest)) {
          UnsignedWidths[Dest] = UnsignedBits;
          Progress = true;
        }
      }
  } while (Progress);

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB) {
      if (MI.isBundled() ||
          llvm::any_of(MI.operands(), [](const MachineOperand &MO) {
            return MO.isReg() && (MO.isUndef() || MO.getSubReg());
          }))
        continue;
      unsigned Bits = getExtensionWidth(MI.getOpcode());
      bool Redundant = Bits && OperandWidth(MI.getOperand(1)) <= Bits;
      if (MI.getOpcode() == RISCC::ANDI || MI.getOpcode() == RISCC::ANDI32) {
        APInt Mask(FullWidth, MI.getOperand(2).getImm());
        Redundant = Mask.isMask() && OperandUnsignedWidth(MI.getOperand(1)) <=
                                         Mask.countTrailingOnes();
      }
      if (MI.getOpcode() == RISCC::AND || MI.getOpcode() == RISCC::AND32)
        for (unsigned I : {1u, 2u}) {
          auto Mask = getConstant(MF, MI.getOperand(I), FullWidth);
          if (!Mask || !Mask->isMask() ||
              OperandUnsignedWidth(MI.getOperand(3 - I)) >
                  Mask->countTrailingOnes())
            continue;
          // Put the surviving value in COPY's source position, regardless
          // of which side held the constant mask.
          if (I == 1)
            MI.getOperand(1).setReg(MI.getOperand(2).getReg());
          Redundant = true;
          break;
        }
      if (Redundant) {
        if (MI.getOperand(1).isTied())
          MI.untieRegOperand(1);
        while (MI.getNumOperands() > 2)
          MI.removeOperand(MI.getNumOperands() - 1);
        MI.getOperand(1).setIsKill(false);
        MI.setDesc(TII.get(TargetOpcode::COPY));
        Changed = true;
      }
    }
  return Changed;
}

// Track the contribution of one ADDI through machine SSA, as in
// RISCVFoldMemOffset. Remove it only when every affected value ultimately
// feeds a native-word address with a legal combined displacement. Unlike
// RISC-V, byte and halfword operations have no displacement to absorb here.
// Keep the original definition as a COPY, so even a physical input is sampled
// at the same point; subsequent copy propagation can remove the temporary.
static bool foldAddressOffsets(MachineFunction &MF) {
  if (!EnableFoldAddressOffsets || !MF.getRegInfo().isSSA())
    return false;
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const unsigned Bits = STI.isRC32() ? 32 : 16;
  const uint32_t Mask = STI.isRC32() ? UINT32_MAX : UINT16_MAX;
  const unsigned AddOp = TII.getNativeOpcode(RISCC::ADDI);
  auto PlainOperands = [](const MachineInstr &MI) {
    return !MI.isBundled() && !MI.getNumImplicitOperands() &&
           llvm::none_of(MI.operands(), [](const MachineOperand &MO) {
             return MO.isReg() && (MO.isUndef() || MO.getSubReg());
           });
  };
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &Root : MBB) {
      if (Root.getOpcode() != AddOp || !PlainOperands(Root) ||
          !Root.getOperand(0).getReg().isVirtual() ||
          Root.getOperand(2).getImm() == 0)
        continue;
      Register Result = Root.getOperand(0).getReg();
      DenseMap<Register, uint32_t> Offsets;
      Offsets[Result] = uint32_t(Root.getOperand(2).getImm()) & Mask;
      SmallVector<Register, 16> Worklist{Result};
      SmallPtrSet<MachineInstr *, 16> Arithmetic{&Root}, Memory;
      unsigned Examined = 0;
      bool Safe = true;
      while (!Worklist.empty() && Safe) {
        Register Reg = Worklist.pop_back_val();
        if (++Examined > 64) {
          Safe = false;
          break;
        }
        for (MachineOperand &Use : MRI.use_nodbg_operands(Reg)) {
          MachineInstr &User = *Use.getParent();
          if (!PlainOperands(User)) {
            Safe = false;
            break;
          }
          unsigned Op = User.getOpcode();
          if (Op == RISCC::LD || Op == RISCC::LD32 || Op == RISCC::ST ||
              Op == RISCC::ST32) {
            // An address can be compensated, but stored data cannot.
            if (Use.getOperandNo() != 1 || !User.getOperand(2).isImm()) {
              Safe = false;
              break;
            }
            Memory.insert(&User);
            continue;
          }
          if (User.getNumExplicitDefs() != 1 || !User.getOperand(0).isReg() ||
              !User.getOperand(0).getReg().isVirtual() ||
              !User.getOperand(1).isReg()) {
            Safe = false;
            break;
          }
          uint32_t Offset = Offsets.lookup(User.getOperand(1).getReg());
          switch (Op) {
          case TargetOpcode::COPY:
          case RISCC::MOV:
          case RISCC::MOV32:
          case RISCC::ADDI:
          case RISCC::ADDI32:
            break;
          case RISCC::ADD:
          case RISCC::ADD32:
            Offset += Offsets.lookup(User.getOperand(2).getReg());
            break;
          case RISCC::SUB:
          case RISCC::SUB32:
            Offset -= Offsets.lookup(User.getOperand(2).getReg());
            break;
          case RISCC::SLLI:
          case RISCC::SLLI32: {
            unsigned Amount = User.getOperand(2).getImm();
            Offset = Amount >= Bits ? 0 : Offset << Amount;
            break;
          }
          default:
            Safe = false;
            break;
          }
          if (!Safe)
            break;
          Offset &= Mask;
          Arithmetic.insert(&User);
          Register Dst = User.getOperand(0).getReg();
          auto [It, New] = Offsets.try_emplace(Dst, Offset);
          if (New || It->second != Offset) {
            It->second = Offset;
            Worklist.push_back(Dst);
          }
        }
      }
      if (!Safe || Memory.empty())
        continue;
      DenseMap<MachineInstr *, int64_t> NewOffsets;
      for (MachineInstr *MI : Memory) {
        uint32_t Combined = (uint64_t(MI->getOperand(2).getImm()) +
                             Offsets.lookup(MI->getOperand(1).getReg())) &
                            Mask;
        int64_t Offset = SignExtend64(Combined, Bits);
        if (!STI.isLegalWordOffset(Offset)) {
          Safe = false;
          break;
        }
        NewOffsets[MI] = Offset;
      }
      if (!Safe)
        continue;
      for (auto [MI, Offset] : NewOffsets)
        MI->getOperand(2).setImm(Offset);
      for (MachineInstr *MI : Arithmetic) {
        if (Offsets.lookup(MI->getOperand(0).getReg()))
          MRI.markUsesInDebugValueAsUndef(MI->getOperand(0).getReg());
        MI->clearFlag(MachineInstr::NoUWrap);
        MI->clearFlag(MachineInstr::NoSWrap);
        MI->clearFlag(MachineInstr::IsExact);
      }
      if (Root.getOperand(1).isTied())
        Root.untieRegOperand(1);
      Root.removeOperand(2);
      Root.getOperand(1).setIsKill(false);
      Root.setDesc(TII.get(TargetOpcode::COPY));
      Changed = true;
    }
  return Changed;
}

// Follow a normalization's result through machine SSA. The low Bits of each
// queued value are unchanged; a narrow store or another normalization can
// discard the differing high bits. Unknown users, including addresses and
// physical-register call/return copies, require the original full value.
// This is the same all-users approach as RISCVOptWInstrs, for either native
// RISC-C width rather than for RV64's word operations.
static bool upperBitsAreUnused(MachineFunction &MF, MachineInstr &Original,
                               unsigned OriginalBits,
                               DenseMap<MachineInstr *, unsigned> &Affected) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  unsigned FullWidth = MF.getSubtarget<RISCCSubtarget>().isRC32() ? 32 : 16;
  SmallVector<std::pair<MachineInstr *, unsigned>, 16> Worklist{
      {&Original, OriginalBits}};
  unsigned Examined = 0;
  while (!Worklist.empty()) {
    auto [MI, Bits] = Worklist.pop_back_val();
    auto [It, New] = Affected.try_emplace(MI, Bits);
    if (!New && It->second <= Bits)
      continue;
    It->second = Bits;
    if (++Examined > 64)
      return false;
    // A left shift may discard every changed bit. Its result is unchanged,
    // but the widened input can still invalidate its no-wrap flags.
    if (Bits >= FullWidth)
      continue;
    if (MI->getNumExplicitDefs() != 1 || !MI->getOperand(0).isReg() ||
        MI->getOperand(0).getSubReg() ||
        !MI->getOperand(0).getReg().isVirtual())
      return false;
    for (MachineOperand &Use :
         MRI.use_nodbg_operands(MI->getOperand(0).getReg())) {
      MachineInstr *User = Use.getParent();
      unsigned Operand = Use.getOperandNo();
      if (Use.getSubReg() || Use.isImplicit() || Use.isUndef() ||
          User->isBundled() || User->isInlineAsm())
        return false;
      unsigned ResultBits = Bits;
      switch (User->getOpcode()) {
      case RISCC::STB:
      case RISCC::STB32:
        if (Operand == 0 && Bits >= 8)
          continue;
        return false;
      case RISCC::STH:
        if (Operand == 0 && Bits >= 16)
          continue;
        return false;
      case RISCC::SEXT8_RC16:
      case RISCC::SEXT8_RC32:
      case RISCC::SEXT16_RC32:
      case RISCC::SEXT16_SHIFT_RC32:
        if (Operand == 1 && Bits >= getExtensionWidth(User->getOpcode()))
          continue;
        return false;
      case RISCC::ANDI:
      case RISCC::ANDI32:
        if (APInt(FullWidth, User->getOperand(2).getImm()).getActiveBits() <=
            Bits)
          continue;
        break;
      case RISCC::AND:
      case RISCC::AND32:
        if (auto C = getConstant(MF, User->getOperand(3 - Operand), FullWidth);
            C && C->getActiveBits() <= Bits)
          continue;
        break;
      case RISCC::SLLI:
      case RISCC::SLLI32:
        ResultBits += User->getOperand(2).getImm();
        break;
      case RISCC::SRLI:
      case RISCC::SRLI32:
      case RISCC::SRAI:
      case RISCC::SRAI32: {
        unsigned Amount = User->getOperand(2).getImm();
        if (Amount >= Bits)
          return false;
        ResultBits -= Amount;
        break;
      }
      case TargetOpcode::COPY:
      case TargetOpcode::PHI:
      case RISCC::MOV:
      case RISCC::MOV32:
      case RISCC::ADD:
      case RISCC::ADD32:
      case RISCC::ADDI:
      case RISCC::ADDI32:
      case RISCC::SUB:
      case RISCC::SUB32:
      case RISCC::MUL:
      case RISCC::MUL32:
      case RISCC::OR:
      case RISCC::OR32:
      case RISCC::ORI:
      case RISCC::ORI32:
      case RISCC::XOR:
      case RISCC::XOR32:
      case RISCC::XORI:
      case RISCC::XORI32:
        break;
      default:
        return false;
      }
      Worklist.emplace_back(User, ResultBits);
    }
  }
  return true;
}

static bool removeUnusedUpperBits(MachineFunction &MF) {
  // Removing a normalization can change coalescing and register pressure.
  // Prefer the smaller instruction sequence for size; without a scheduling
  // cost model it is not necessarily a win for speed-optimized functions.
  if (EnableUnusedUpperBits == cl::boolOrDefault::BOU_FALSE ||
      (EnableUnusedUpperBits == cl::boolOrDefault::BOU_UNSET &&
       !MF.getFunction().hasOptSize()) ||
      !MF.getRegInfo().isSSA())
    return false;
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  unsigned FullWidth = STI.isRC32() ? 32 : 16;
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB) {
      if (MI.isBundled() || MI.getNumImplicitOperands() ||
          llvm::any_of(MI.operands(), [](const MachineOperand &MO) {
            return MO.isReg() && (MO.isUndef() || MO.getSubReg());
          }))
        continue;
      unsigned Bits = getExtensionWidth(MI.getOpcode()), Source = 1;
      if (MI.getOpcode() == RISCC::ANDI || MI.getOpcode() == RISCC::ANDI32) {
        APInt Mask(FullWidth, MI.getOperand(2).getImm());
        if (Mask.isMask())
          Bits = Mask.countTrailingOnes();
      } else if (MI.getOpcode() == RISCC::AND ||
                 MI.getOpcode() == RISCC::AND32) {
        for (unsigned I : {1u, 2u})
          if (auto Mask = getConstant(MF, MI.getOperand(I), FullWidth);
              Mask && Mask->isMask()) {
            Bits = Mask->countTrailingOnes();
            Source = 3 - I;
            break;
          }
      }
      if (!Bits || Bits >= FullWidth)
        continue;
      DenseMap<MachineInstr *, unsigned> Affected;
      if (!upperBitsAreUnused(MF, MI, Bits, Affected))
        continue;
      // Upper-bit changes can invalidate debug values and arithmetic flags
      // even though every real consumer observes the same result.
      for (auto [Use, UnchangedBits] : Affected) {
        if (UnchangedBits < FullWidth)
          MRI.markUsesInDebugValueAsUndef(Use->getOperand(0).getReg());
        Use->clearFlag(MachineInstr::NoUWrap);
        Use->clearFlag(MachineInstr::NoSWrap);
        Use->clearFlag(MachineInstr::IsExact);
      }
      if (MI.getOperand(1).isTied())
        MI.untieRegOperand(1);
      MI.getOperand(1).setReg(MI.getOperand(Source).getReg());
      MI.getOperand(1).setIsKill(false);
      while (MI.getNumOperands() > 2)
        MI.removeOperand(MI.getNumOperands() - 1);
      MI.setDesc(TII.get(TargetOpcode::COPY));
      Changed = true;
    }
  return Changed;
}

// CMPI is also a three-address immediate subtraction with its result fixed
// to R0. After allocation, fold a copied input followed by an ADDI to R0.
// This applies to arithmetic results, not just comparisons or branches.
static bool foldR0ImmediateAdds(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  if (STI.isNano())
    return false;
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  const unsigned AddOp = TII.getNativeOpcode(RISCC::ADDI);
  const unsigned MoveOp = TII.getNativeOpcode(RISCC::MOV);
  const unsigned CompareOp = TII.getNativeOpcode(RISCC::CMPI);
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    bool BlockChanged = false;
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &Add = *I++;
      if (Add.getOpcode() != AddOp || Add.getNumOperands() != 3 ||
          Add.getOperand(0).getReg() != RISCC::R0 ||
          Add.getOperand(1).getReg() != RISCC::R0 ||
          Add.getOperand(1).isUndef() ||
          Add.getFlag(MachineInstr::FrameSetup) ||
          Add.getFlag(MachineInstr::FrameDestroy))
        continue;
      int64_t Imm = Add.getOperand(2).getImm();
      if (!isInt<8>(Imm) || !isInt<8>(-Imm))
        continue;
      MachineInstr *Copy = nullptr;
      unsigned Scanned = 0;
      for (auto J = Add.getIterator(); J != MBB.begin() && Scanned++ != 16;) {
        MachineInstr &MI = *--J;
        if (MI.isDebugInstr() || MI.isCall() || MI.isTerminator() ||
            MI.isInlineAsm() || MI.isCFIInstruction() ||
            MI.hasUnmodeledSideEffects() || MI.readsRegister(RISCC::R0, &TRI))
          break;
        if (MI.modifiesRegister(RISCC::R0, &TRI)) {
          if (MI.getOpcode() == MoveOp && MI.getNumOperands() == 2 &&
              !MI.getOperand(1).isUndef() && !MI.getOperand(1).getSubReg() &&
              !MI.getFlag(MachineInstr::FrameSetup) &&
              !MI.getFlag(MachineInstr::FrameDestroy))
            Copy = &MI;
          break;
        }
      }
      if (!Copy)
        continue;
      Register Source = Copy->getOperand(1).getReg();
      if (llvm::any_of(
              make_range(std::next(Copy->getIterator()), Add.getIterator()),
              [&](const MachineInstr &MI) {
                return MI.modifiesRegister(Source, &TRI);
              }))
        continue;
      BuildMI(MBB, Add, Add.getDebugLoc(), TII.get(CompareOp))
          .addReg(Source)
          .addImm(-Imm);
      Copy->eraseFromParent();
      Add.eraseFromParent();
      BlockChanged = true;
    }
    if (BlockChanged) {
      recomputeLivenessFlags(MBB);
      Changed = true;
    }
  }
  return Changed;
}

static bool isPhysicalValueBarrier(const MachineInstr &MI) {
  return MI.isDebugInstr() || MI.isCall() || MI.isTerminator() ||
         MI.isInlineAsm() || MI.isCFIInstruction() ||
         MI.hasUnmodeledSideEffects() || MI.getFlag(MachineInstr::FrameSetup) ||
         MI.getFlag(MachineInstr::FrameDestroy);
}

// Merge updates whose intermediate value is unobserved. In particular, an
// adjustment undone after an unrelated spill does not need either ADDI.
static bool foldPhysicalImmediateChains(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  const unsigned AddOp = TII.getNativeOpcode(RISCC::ADDI);
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    bool BlockChanged = false;
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &Add = *I++;
      if (Add.getOpcode() != AddOp || isPhysicalValueBarrier(Add) ||
          Add.getNumOperands() != 3 || Add.getOperand(1).isUndef() ||
          !isInt<8>(Add.getOperand(2).getImm()))
        continue;
      Register Dst = Add.getOperand(0).getReg();
      if (Dst == RISCC::R7 || Dst != Add.getOperand(1).getReg())
        continue;
      unsigned Scanned = 0;
      for (auto J = Add.getIterator(); J != MBB.begin() && Scanned++ != 16;) {
        MachineInstr &Prev = *--J;
        if (isPhysicalValueBarrier(Prev))
          break;
        if (Prev.getOpcode() == AddOp && Prev.getNumOperands() == 3 &&
            Prev.getOperand(0).getReg() == Dst &&
            Prev.getOperand(1).getReg() == Dst &&
            !Prev.getOperand(1).isUndef() &&
            isInt<8>(Prev.getOperand(2).getImm())) {
          int64_t Sum =
              Prev.getOperand(2).getImm() + Add.getOperand(2).getImm();
          if (isInt<8>(Sum)) {
            Prev.eraseFromParent();
            if (Sum)
              Add.getOperand(2).setImm(Sum);
            else
              Add.eraseFromParent();
            BlockChanged = true;
          }
          break;
        }
        if (Prev.readsRegister(Dst, &TRI) || Prev.modifiesRegister(Dst, &TRI))
          break;
      }
    }
    if (BlockChanged) {
      recomputeLivenessFlags(MBB);
      Changed = true;
    }
  }
  return Changed;
}

// Hoist a pure two-address computation when its physical result register is
// otherwise unchanged throughout the loop. This avoids creating a new live
// virtual register or asking allocation to speculate about register pressure.
static bool hoistInvariantCopies(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  unsigned MoveOp = TII.getNativeOpcode(RISCC::MOV);
  auto IsImmediateOp = [](unsigned Op) {
    return Op == RISCC::ANDI || Op == RISCC::ANDI32 || Op == RISCC::ORI ||
           Op == RISCC::ORI32 || Op == RISCC::XORI || Op == RISCC::XORI32 ||
           Op == RISCC::ADDI || Op == RISCC::ADDI32;
  };
  MachineDominatorTree MDT(MF);
  MachineLoopInfo MLI(MDT);
  bool Changed = false;
  for (MachineLoop *Loop : MLI.getLoopsInPreorder()) {
    MachineBasicBlock *Header = Loop->getHeader();
    if (llvm::any_of(Loop->blocks(), [](const MachineBasicBlock *BB) {
          return BB->isEHPad() || BB->hasAddressTaken();
        }))
      continue;
    MachineBasicBlock *Entry = nullptr;
    bool SafeEntry = true;
    for (MachineBasicBlock *Pred : Header->predecessors())
      if (!Loop->contains(Pred)) {
        if (Entry && Pred != Entry)
          SafeEntry = false;
        Entry = Pred;
      }
    if (!SafeEntry || !Entry || Entry->isEHPad())
      continue;
    for (auto I = Header->begin(); I != Header->end();) {
      MachineInstr &Copy = *I++;
      if (Copy.getOpcode() != MoveOp || Copy.getNumOperands() != 2 ||
          isPhysicalValueBarrier(Copy) || Copy.getOperand(1).isUndef())
        continue;
      Register Dst = Copy.getOperand(0).getReg();
      Register Src = Copy.getOperand(1).getReg();
      if (Dst == RISCC::R7 || Dst == Src || !STI.getGPRClass()->contains(Dst) ||
          !STI.getGPRClass()->contains(Src))
        continue;
      MachineInstr *Op = nullptr;
      for (auto J = I; J != Header->end(); ++J) {
        if (isPhysicalValueBarrier(*J))
          break;
        if (IsImmediateOp(J->getOpcode()) && J->getNumOperands() == 3 &&
            J->getOperand(0).getReg() == Dst &&
            J->getOperand(1).getReg() == Dst && !J->getOperand(1).isUndef()) {
          Op = &*J;
          break;
        }
        if (J->readsRegister(Dst, &TRI) || J->modifiesRegister(Dst, &TRI))
          break;
      }
      if (!Op)
        continue;
      bool Safe = true;
      for (MachineBasicBlock *BB : Loop->blocks())
        for (MachineInstr &MI : *BB) {
          if (MI.isCall() || MI.isInlineAsm() || MI.isCFIInstruction() ||
              MI.hasUnmodeledSideEffects() || MI.isBundled() ||
              MI.getFlag(MachineInstr::FrameSetup) ||
              MI.getFlag(MachineInstr::FrameDestroy) ||
              MI.modifiesRegister(Src, &TRI) ||
              (&MI != &Copy && &MI != Op && MI.modifiesRegister(Dst, &TRI)))
            Safe = false;
        }
      for (MachineInstr &MI :
           make_range(Header->instr_begin(), Copy.getIterator()))
        if (MI.readsRegister(Dst, &TRI))
          Safe = false;
      for (MachineInstr &MI :
           make_range(Entry->getFirstTerminator(), Entry->end()))
        if (MI.readsRegister(Dst, &TRI) || MI.modifiesRegister(Dst, &TRI) ||
            MI.modifiesRegister(Src, &TRI) || MI.isCall() || MI.isInlineAsm() ||
            MI.isBundled() || MI.hasUnmodeledSideEffects() ||
            MI.getFlag(MachineInstr::FrameSetup) ||
            MI.getFlag(MachineInstr::FrameDestroy))
          Safe = false;
      // A pure computation may be speculated onto another successor only
      // if it cannot destroy a live incoming value on that path.
      for (MachineBasicBlock *Succ : Entry->successors())
        if (Succ != Header && Succ->isLiveIn(Dst))
          Safe = false;
      if (!Safe)
        continue;
      if (I == Op->getIterator())
        ++I;
      Entry->splice(Entry->getFirstTerminator(), Header, Copy.getIterator());
      Entry->splice(Entry->getFirstTerminator(), Header, Op->getIterator());
      Copy.setDebugLoc(DebugLoc());
      Op->setDebugLoc(DebugLoc());
      for (MachineBasicBlock *BB : Loop->blocks()) {
        if (!BB->isLiveIn(Dst))
          BB->addLiveIn(Dst);
        BB->sortUniqueLiveIns();
      }
      Changed = true;
    }
  }
  if (Changed)
    for (MachineBasicBlock &MBB : MF)
      recomputeLivenessFlags(MBB);
  return Changed;
}

// A physical value is an opaque definition plus a native-width constant.
// Copies preserve that identity, including transfers to spill-cache registers.
// Memory facts are limited to immutable literal-pool values. Equality facts
// apply to individual forward edges, never an entire predecessor exit.
struct PhysicalValue {
  unsigned Base;
  uint32_t Offset;
  bool operator==(const PhysicalValue &Other) const {
    return Base == Other.Base && Offset == Other.Offset;
  }
};

// Use facts found after allocation, including copies through S registers and
// equality facts on forward edges. Only a single analyzable condition is safe:
// a second conditional branch may observe a different set of incoming paths.
static bool foldConstantBranch(MachineBasicBlock &MBB, uint32_t Value,
                               const RISCCSubtarget &STI) {
  const auto &TII = *STI.getInstrInfo();
  if (MBB.succ_size() != 2 || MBB.isEHPad() || MBB.hasAddressTaken())
    return false;
  MachineBasicBlock *True = nullptr, *False = nullptr;
  SmallVector<MachineOperand, 1> Cond;
  if (TII.analyzeBranch(MBB, True, False, Cond, false) || !True ||
      Cond.size() != 1)
    return false;
  if (!False)
    False = MBB.getNextNode();
  if (!False || True == False || !MBB.isSuccessor(True) ||
      !MBB.isSuccessor(False))
    return false;
  if (llvm::any_of(make_range(MBB.getFirstTerminator(), MBB.end()),
                   [](const MachineInstr &MI) {
                     return MI.isBundled() ||
                            llvm::any_of(MI.operands(),
                                         [](const MachineOperand &MO) {
                                           return MO.isReg() && MO.isUndef();
                                         });
                   }))
    return false;

  bool Taken;
  switch (Cond[0].getImm()) {
  default:
    return false;
  case RISCC::BEQZ:
    Taken = Value == 0;
    break;
  case RISCC::BNEZ:
    Taken = Value != 0;
    break;
  case RISCC::BLTZ:
    Taken = SignExtend64(Value, STI.isRC32() ? 32 : 16) < 0;
    break;
  case RISCC::BGEZ:
    Taken = SignExtend64(Value, STI.isRC32() ? 32 : 16) >= 0;
    break;
  }
  MachineBasicBlock *Dest = Taken ? True : False;
  DebugLoc DL = MBB.findBranchDebugLoc();
  TII.removeBranch(MBB);
  if (Dest != MBB.getNextNode() || Dest->getSectionID() != MBB.getSectionID())
    TII.insertBranch(MBB, Dest, nullptr, {}, DL);
  MachineBasicBlock *DeadEdge = Taken ? False : True;
  DeadEdge->removePHIsIncomingValuesForPredecessor(MBB);
  MBB.removeSuccessor(DeadEdge, /*NormalizeSuccProbs=*/true);
  ++NumConstantBranches;
  return true;
}

static bool removeRepeatedPhysicalValues(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  using State = SmallVector<PhysicalValue, 17>;
  DenseMap<const MachineBasicBlock *, State> ExitValues;
  DenseMap<unsigned, PhysicalValue> LiteralValues;
  unsigned NextValue = 1;
  const uint32_t Mask = STI.isRC32() ? UINT32_MAX : UINT16_MAX;
  const unsigned MoveOp = TII.getNativeOpcode(RISCC::MOV);
  const unsigned AddOp = TII.getNativeOpcode(RISCC::ADDI);
  const unsigned LoadOp = TII.getNativeOpcode(RISCC::LDI);
  const unsigned CompareOp = TII.getNativeOpcode(RISCC::CMPI);
  auto Fresh = [&]() { return PhysicalValue{NextValue++, 0}; };
  auto IsStorageReg = [&](Register Reg) {
    // S0 is the asynchronously written exception PC; S1 is reserved for TLS.
    return Reg.isPhysical() && Reg != RISCC::S0 && Reg != RISCC::S1 &&
           (STI.getGPRClass()->contains(Reg) ||
            (!STI.isNano() && STI.getSRegClass()->contains(Reg)));
  };
  auto EdgeValues = [&](MachineBasicBlock *Pred, MachineBasicBlock *Succ) {
    State Edge = ExitValues.find(Pred)->second;
    if (!EnableBranchEquality || !MF.getFunction().hasOptSize() ||
        Pred->succ_size() != 2)
      return Edge;
    MachineBasicBlock *True = nullptr, *False = nullptr;
    SmallVector<MachineOperand, 1> Cond;
    if (TII.analyzeBranch(*Pred, True, False, Cond, false) || Cond.size() != 1)
      return Edge;
    unsigned Opcode = Cond[0].getImm();
    if (Opcode != RISCC::BEQZ && Opcode != RISCC::BNEZ)
      return Edge;
    if (!False)
      False = Pred->getNextNode();
    if (!True || !False || True == False || !Pred->isSuccessor(True) ||
        !Pred->isSuccessor(False) ||
        Succ != (Opcode == RISCC::BEQZ ? True : False))
      return Edge;
    PhysicalValue Zero = Edge[RISCC::R0];
    // A constant already needs no inference. A nonzero constant would make
    // this edge unreachable; do not manufacture facts from that contradiction.
    if (Zero.Base == 0)
      return Edge;
    // If R0 = V+A = 0, any still-available V+B equals B-A. This includes
    // the unchanged source of CMPI and copies kept in spill-cache registers.
    // Do not turn stack-pointer updates into redundant-value candidates.
    for (unsigned Reg = 1; Reg != Edge.size(); ++Reg)
      if (Reg != RISCC::R7 && IsStorageReg(Reg) && Edge[Reg].Base == Zero.Base)
        Edge[Reg] = {0, (Edge[Reg].Offset - Zero.Offset) & Mask};
    return Edge;
  };
  bool Changed = false;
  ReversePostOrderTraversal<MachineFunction *> RPO(&MF);
  for (MachineBasicBlock *MBB : RPO) {
    DenseMap<int64_t, PhysicalValue> StackValues;
    State Values(TRI.getNumRegs());
    for (PhysicalValue &V : Values)
      V = Fresh();
    // Meet facts from known forward edges. Reset at loop headers: a backedge
    // must never be treated as another iteration with the same definitions.
    if (!MBB->pred_empty() && !MBB->isEHPad() && !MBB->hasAddressTaken() &&
        (MBB->pred_size() == 1 || EnableJoinValues)) {
      bool Known = llvm::all_of(MBB->predecessors(), [&](auto *Pred) {
        return ExitValues.contains(Pred);
      });
      if (Known) {
        SmallVector<State, 4> Incoming;
        for (MachineBasicBlock *Pred : MBB->predecessors())
          Incoming.push_back(EdgeValues(Pred, MBB));
        const State &First = Incoming.front();
        for (unsigned Reg = 1; Reg != Values.size(); ++Reg) {
          if (!IsStorageReg(Reg))
            continue;
          if (llvm::all_of(Incoming, [&](const State &V) {
                return V[Reg] == First[Reg];
              })) {
            Values[Reg] = First[Reg];
            continue;
          }
          // Equalities can survive a join even when the individual incoming
          // definitions differ. Keep only relations proved on every edge.
          for (unsigned Other = 1; Other < Reg; ++Other) {
            if (!IsStorageReg(Other) || First[Reg].Base != First[Other].Base)
              continue;
            uint32_t Delta = (First[Reg].Offset - First[Other].Offset) & Mask;
            if (llvm::all_of(Incoming, [&](const State &V) {
                  return V[Reg].Base == V[Other].Base &&
                         ((V[Reg].Offset - V[Other].Offset) & Mask) == Delta;
                })) {
              Values[Reg] = Values[Other];
              Values[Reg].Offset = (Values[Reg].Offset + Delta) & Mask;
              break;
            }
          }
        }
      }
    }
    bool SawTerminator = false;
    for (auto I = MBB->begin(); I != MBB->end();) {
      MachineInstr &MI = *I++;
      if (MI.isDebugInstr())
        continue;
      if (EnablePreservedValues && MI.isCall() && !MI.isInlineAsm() &&
          !MI.getFlag(MachineInstr::FrameSetup) &&
          !MI.getFlag(MachineInstr::FrameDestroy) &&
          llvm::any_of(MI.operands(), [](const MachineOperand &MO) {
            return MO.isRegMask();
          })) {
        // Calls may change any memory, but the explicit definitions and the
        // call mask fully describe their effects on physical registers.
        for (unsigned Reg = 1; Reg != Values.size(); ++Reg)
          if (MI.modifiesRegister(Reg, &TRI))
            Values[Reg] = Fresh();
        StackValues.clear();
        SawTerminator |= MI.isTerminator();
        continue;
      }
      if (MI.isCall() || MI.isInlineAsm() || MI.hasUnmodeledSideEffects() ||
          MI.getFlag(MachineInstr::FrameSetup) ||
          MI.getFlag(MachineInstr::FrameDestroy) ||
          (SawTerminator && !MI.isTerminator())) {
        for (PhysicalValue &V : Values)
          V = Fresh();
        StackValues.clear();
        continue;
      }
      if (MI.mayStore() || MI.modifiesRegister(RISCC::R7, &TRI))
        StackValues.clear();
      SawTerminator |= MI.isTerminator();
      Register Dst;
      std::optional<PhysicalValue> Result;
      unsigned Op = MI.getOpcode();
      bool IsLiteral = false;
      // If D already contains S+A, rebuilding S+B needs only D += B-A.
      // The copied intermediate value must not be observed. Leave memory
      // instructions in place and preserve the original instruction count
      // unless the MOV can actually be deleted.
      if (EnableAffineValues && Op == MoveOp && MI.getNumOperands() == 2 &&
          !MI.getOperand(1).isUndef() && !isPhysicalValueBarrier(MI)) {
        Register D = MI.getOperand(0).getReg();
        Register S = MI.getOperand(1).getReg();
        if (D != RISCC::R7 && Values[D].Base == Values[S].Base) {
          unsigned Scanned = 0;
          MachineInstr *Add = nullptr;
          for (auto J = I; J != MBB->end() && Scanned++ != 16; ++J) {
            if (isPhysicalValueBarrier(*J))
              break;
            if (J->getOpcode() == AddOp && J->getNumOperands() == 3 &&
                J->getOperand(0).getReg() == D &&
                J->getOperand(1).getReg() == D && !J->getOperand(1).isUndef()) {
              Add = &*J;
              break;
            }
            if (J->readsRegister(D, &TRI) || J->modifiesRegister(D, &TRI))
              break;
          }
          if (Add) {
            uint32_t Delta = (Values[S].Offset + Add->getOperand(2).getImm() -
                              Values[D].Offset) &
                             Mask;
            int64_t SignedDelta = SignExtend64(Delta, STI.isRC32() ? 32 : 16);
            if (isInt<8>(SignedDelta)) {
              Add->getOperand(2).setImm(SignedDelta);
              MI.eraseFromParent();
              Changed = true;
              continue;
            }
          }
        }
      }
      if ((Op == MoveOp || Op == RISCC::MFS || Op == RISCC::MTS) &&
          MI.getNumOperands() == 2 && IsStorageReg(MI.getOperand(0).getReg()) &&
          IsStorageReg(MI.getOperand(1).getReg()) &&
          !MI.getOperand(1).isUndef()) {
        Dst = MI.getOperand(0).getReg();
        Result = Values[MI.getOperand(1).getReg()];
      } else if (Op == CompareOp && MI.getNumOperands() == 3 &&
                 !MI.getOperand(0).isUndef()) {
        Dst = RISCC::R0;
        Result = Values[MI.getOperand(0).getReg()];
        Result->Offset = (Result->Offset - MI.getOperand(1).getImm()) & Mask;
      } else if (Op == AddOp && MI.getNumOperands() == 3 &&
                 !MI.getOperand(1).isUndef()) {
        Dst = MI.getOperand(0).getReg();
        Result = Values[MI.getOperand(1).getReg()];
        Result->Offset = (Result->Offset + MI.getOperand(2).getImm()) & Mask;
      } else if (Op == LoadOp && MI.getNumOperands() == 2) {
        Dst = MI.getOperand(0).getReg();
        Result = PhysicalValue{0, uint32_t(MI.getOperand(1).getImm()) & Mask};
      } else if (EnableLiteralValues && Op == RISCC::LDPC &&
                 MI.getNumOperands() == 2 && MI.getOperand(1).isCPI() &&
                 MI.getOperand(1).getOffset() == 0 &&
                 !MI.hasOrderedMemoryRef()) {
        unsigned CPI = MI.getOperand(1).getIndex();
        const auto &Entry = MF.getConstantPool()->getConstants()[CPI];
        Dst = MI.getOperand(0).getReg();
        if (!Entry.isMachineConstantPoolEntry() &&
            isa<ConstantInt>(Entry.Val.ConstVal)) {
          const auto *C = cast<ConstantInt>(Entry.Val.ConstVal);
          if (C->getBitWidth() == (STI.isRC32() ? 32u : 16u))
            Result = PhysicalValue{0, uint32_t(C->getZExtValue()) & Mask};
        } else {
          auto It = LiteralValues.try_emplace(CPI, Fresh()).first;
          Result = It->second;
        }
        IsLiteral = Result.has_value();
      } else if (EnableLiteralValues && !STI.isRC32() &&
                 (Op == RISCC::LDI16 || Op == RISCC::LUI) &&
                 MI.getNumOperands() == 2 && MI.getOperand(1).isImm()) {
        Dst = MI.getOperand(0).getReg();
        uint32_t Value = MI.getOperand(1).getImm();
        Result =
            PhysicalValue{0, (Op == RISCC::LUI ? Value << 8 : Value) & Mask};
        IsLiteral = true;
      } else if (EnableStackValues && Op == TII.getNativeOpcode(RISCC::LD) &&
                 MI.getNumOperands() == 3 &&
                 MI.getOperand(0).getReg() != RISCC::R7 &&
                 MI.getOperand(1).getReg() == RISCC::R7 &&
                 MI.getOperand(2).isImm() && MI.hasOneMemOperand() &&
                 !MI.hasOrderedMemoryRef() &&
                 isa_and_nonnull<FixedStackPseudoSourceValue>(
                     (*MI.memoperands_begin())->getPseudoValue())) {
        Dst = MI.getOperand(0).getReg();
        int64_t Offset = MI.getOperand(2).getImm();
        auto Prior = StackValues.find(Offset);
        Result = Prior == StackValues.end() ? Fresh() : Prior->second;
        if (Prior != StackValues.end())
          for (MCPhysReg Reg : *STI.getGPRClass()) {
            if (Reg == RISCC::R7 || !(Values[Reg] == *Result))
              continue;
            MI.setDesc(STI.getInstrInfo()->get(MoveOp));
            MI.getOperand(1).setReg(Reg);
            MI.getOperand(1).setIsKill(false);
            MI.removeOperand(2);
            MI.dropMemRefs(MF);
            Changed = true;
            break;
          }
        StackValues[Offset] = *Result;
      }
      // Do not erase stack-pointer updates, even an apparent identity: their
      // position can matter to unwind/debug information.
      if (Result && Dst != RISCC::R7 && Values[Dst] == *Result) {
        MI.eraseFromParent();
        Changed = true;
        continue;
      }
      if (IsLiteral && Dst != RISCC::R7)
        for (MCPhysReg Reg : *STI.getGPRClass()) {
          if (Reg == RISCC::R7 || !(Values[Reg] == *Result))
            continue;
          MI.setDesc(STI.getInstrInfo()->get(MoveOp));
          MI.getOperand(1).ChangeToRegister(Reg, false);
          MI.getOperand(1).setIsRenamable(true);
          MI.dropMemRefs(MF);
          Changed = true;
          break;
        }
      // A nearby known value can supply a literal with one compact ADDI,
      // or CMPI when the destination is R0. Do not add a copy or another
      // live temporary: the replacement must be a single instruction.
      // Pool users can then disappear even when the constant was originally
      // shared at several sites. The same relation also handles RC16 LDI16.
      if (EnableAffineLiterals && MF.getFunction().hasOptSize() && IsLiteral &&
          Dst != RISCC::R7 && MI.getOpcode() == Op && !MI.isBundled()) {
        bool Replaced = false;
        for (MCPhysReg Reg : *STI.getGPRClass()) {
          if (Reg == RISCC::R7 ||
              (Reg != Dst && (Dst != RISCC::R0 || STI.isNano())) ||
              Values[Reg].Base != Result->Base)
            continue;
          int64_t Delta =
              SignExtend64((Result->Offset - Values[Reg].Offset) & Mask,
                           STI.isRC32() ? 32 : 16);
          if (!isInt<8>(Reg == Dst ? Delta : -Delta))
            continue;
          if (Reg == Dst)
            BuildMI(*MBB, MI, MI.getDebugLoc(), TII.get(AddOp))
                .add(MI.getOperand(0))
                .addReg(Reg, RegState::Renamable)
                .addImm(Delta);
          else {
            auto Compare =
                BuildMI(*MBB, MI, MI.getDebugLoc(), TII.get(CompareOp))
                    .addReg(Reg, RegState::Renamable)
                    .addImm(-Delta);
            Compare->getOperand(2).setIsDead(MI.getOperand(0).isDead());
          }
          MI.eraseFromParent();
          Values[Dst] = *Result;
          Changed = true;
          Replaced = true;
          break;
        }
        // I already points past the replaced load. Its only definition
        // is Dst, whose identity has just been updated.
        if (Replaced)
          continue;
      }
      // A cached spill may still have a live copy in a GPR. Expose that copy
      // to ordinary copy propagation, which cannot substitute an S register
      // into an ALU instruction. This never introduces a new instruction.
      if (Op == RISCC::MFS && Result)
        for (MCPhysReg Reg : *STI.getGPRClass()) {
          if (Reg == RISCC::R7 || !(Values[Reg] == *Result))
            continue;
          MI.setDesc(STI.getInstrInfo()->get(MoveOp));
          MI.getOperand(1).setReg(Reg);
          MI.getOperand(1).setIsKill(false);
          Changed = true;
          break;
        }
      for (unsigned Reg = 1; Reg != Values.size(); ++Reg)
        if (MI.modifiesRegister(Reg, &TRI))
          Values[Reg] = Fresh();
      if (Result)
        Values[Dst] = *Result;
    }
    ExitValues[MBB] = std::move(Values);
  }
  // Do not mutate the CFG while traversing its original reverse postorder.
  // analyzeBranch guarantees that the exit R0 is also the condition's R0:
  // only an optional unconditional branch can follow the conditional branch.
  if (EnableConstantBranches)
    for (MachineBasicBlock &MBB : MF) {
      auto It = ExitValues.find(&MBB);
      if (It != ExitValues.end() && It->second[RISCC::R0].Base == 0)
        Changed |= foldConstantBranch(MBB, It->second[RISCC::R0].Offset, STI);
    }
  if (Changed) {
    // Deleting an entry copy/compare can make its destination live across
    // the predecessor edge. Preserve existing live-ins and propagate these
    // additional uses to a fixed point, including reserved spill-cache S regs.
    bool Added;
    do {
      Added = false;
      for (MachineBasicBlock &MBB : llvm::reverse(MF)) {
        LivePhysRegs Live;
        computeLiveIns(Live, MBB);
        for (MCPhysReg Reg : Live)
          if (IsStorageReg(Reg) && Reg != RISCC::R7 && !MBB.isLiveIn(Reg)) {
            MBB.addLiveIn(Reg);
            Added = true;
          }
        MBB.sortUniqueLiveIns();
      }
    } while (Added);
    for (MachineBasicBlock &MBB : MF)
      recomputeLivenessFlags(MBB);
  }
  return Changed;
}

// Decide after allocation and value reuse, when the remaining pool users are
// known. A single LDPC plus its four-byte literal costs at least six bytes;
// one or two compact instructions are smaller without needing a temporary.
// Shared entries retain their loads, since duplicating their synthesis may
// cost more than the literal. Symbolic, offset and ordered loads stay intact.
// Restrict this to size optimization: two ALU instructions can take longer
// than a literal load, even when their combined encoding is smaller.
static bool synthesizeUnsharedConstants(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  if (!STI.isRC32() || !MF.getFunction().hasOptSize())
    return false;
  const auto &TII = *STI.getInstrInfo();
  DenseMap<unsigned, unsigned> Uses;
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      for (const MachineOperand &MO : MI.operands())
        if (MO.isCPI())
          ++Uses[MO.getIndex()];
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I++;
      if (MI.getOpcode() != RISCC::LDPC || MI.getNumOperands() != 2 ||
          !MI.getOperand(1).isCPI() || MI.getOperand(1).getOffset() != 0 ||
          MI.hasOrderedMemoryRef() || MI.isBundled() ||
          MI.getFlag(MachineInstr::FrameSetup) ||
          MI.getFlag(MachineInstr::FrameDestroy) ||
          MI.getOperand(0).getReg() == RISCC::R7)
        continue;
      unsigned CPI = MI.getOperand(1).getIndex();
      const auto &Entry = MF.getConstantPool()->getConstants()[CPI];
      if (Uses.lookup(CPI) != 1 || Entry.isMachineConstantPoolEntry())
        continue;
      const auto *C = dyn_cast<ConstantInt>(Entry.Val.ConstVal);
      if (!C || C->getBitWidth() != 32)
        continue;
      auto Sequence = RISCCMatInt::generate(C->getValue(), STI);
      if (Sequence.empty())
        continue;
      Register Dst = MI.getOperand(0).getReg();
      for (unsigned Index = 0; Index != Sequence.size(); ++Index) {
        const auto &Inst = Sequence[Index];
        auto New = BuildMI(MBB, MI, MI.getDebugLoc(),
                           TII.get(TII.getNativeOpcode(Inst.Opcode)), Dst);
        if (Index)
          New.addReg(Dst, RegState::Kill);
        New.addImm(Inst.Immediate);
        New->getOperand(0).setIsDead(Index + 1 == Sequence.size() &&
                                     MI.getOperand(0).isDead());
      }
      MI.eraseFromParent();
      Changed = true;
    }
  }
  return Changed;
}

// Arithmetic right shifts preserve the native sign bit. Once allocation and
// scheduling expose a dead branch value, an in-place shift is unnecessary if
// its only remaining observation is BLTZ/BGEZ. Keep earlier definitions and
// any shift whose numerical result is still observable on either successor.
static bool removeSignBranchShifts(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  if (!EnableSignBranchShifts || !MF.getFunction().hasOptSize() ||
      STI.isNano() ||
      !MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::TracksLiveness))
    return false;
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  const unsigned ShiftOp = TII.getNativeOpcode(RISCC::SRAI);
  auto IsBarrier = [](const MachineInstr &MI) {
    return isPhysicalValueBarrier(MI) || MI.isBundled() || MI.isBundle() ||
           MI.isMetaInstruction() || MI.isPosition() ||
           MI.getPreInstrSymbol() || MI.getPostInstrSymbol();
  };
  auto IsInPlaceShift = [&](const MachineInstr &MI) {
    if (MI.getOpcode() != ShiftOp || MI.getNumOperands() != 3)
      return false;
    const auto &Dst = MI.getOperand(0);
    const auto &Src = MI.getOperand(1);
    const auto &Amount = MI.getOperand(2);
    return Dst.isReg() && Dst.isDef() && !Dst.isImplicit() &&
           !Dst.getSubReg() && Dst.getReg() == RISCC::R0 && Src.isReg() &&
           Src.isUse() && !Src.isImplicit() && !Src.isUndef() &&
           !Src.getSubReg() && Src.getReg() == RISCC::R0 && Amount.isImm() &&
           Amount.getImm() >= 1 &&
           Amount.getImm() <= (STI.hasWideShift() ? 8 : 1);
  };
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    LivePhysRegs Live(TRI);
    Live.addLiveOuts(MBB);
    bool BlockChanged = false;
    for (auto I = MBB.end(); I != MBB.begin();) {
      MachineInstr &MI = *--I;
      if ((MI.getOpcode() == RISCC::BLTZ || MI.getOpcode() == RISCC::BGEZ) &&
          !MI.isBundled() && !Live.contains(RISCC::R0)) {
        auto Before = I;
        while (Before != MBB.begin()) {
          auto Prev = std::prev(Before);
          if (IsBarrier(*Prev))
            break;
          if (IsInPlaceShift(*Prev)) {
            Prev->eraseFromParent();
            BlockChanged = true;
          } else {
            if (Prev->readsRegister(RISCC::R0, &TRI) ||
                Prev->modifiesRegister(RISCC::R0, &TRI))
              break;
            Before = Prev;
          }
        }
      }
      Live.stepBackward(MI);
    }
    if (BlockChanged) {
      recomputeLivenessFlags(MBB);
      Changed = true;
    }
  }
  return Changed;
}

// Form a native sign mask as 0 - (Src < 0) when that is shorter than a chain
// of arithmetic shifts. Choosing the scratch after allocation requires no
// new local spill or frame change; IPRA still observes its clobber at callers.
// Run after frame lowering and scheduling: only an already available,
// non-pristine register may be used.
static bool optimizeLateSignMasks(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  if (!EnableLateSignMasks || !MF.getFunction().hasOptSize() || STI.isNano() ||
      !MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::TracksLiveness))
    return false;
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  const auto &MRI = MF.getRegInfo();
  const auto &MFI = MF.getFrameInfo();
  const unsigned ShiftOp = TII.getNativeOpcode(RISCC::SRAI);
  const unsigned ZeroOp = TII.getNativeOpcode(RISCC::LDI);
  const unsigned LessOp = TII.getNativeOpcode(RISCC::SLT);
  const unsigned SubOp = TII.getNativeOpcode(RISCC::SUB);
  const unsigned Bits = STI.getXLenVT().getSizeInBits();
  const unsigned NewBytes = TII.get(ZeroOp).getSize() +
                            TII.get(LessOp).getSize() +
                            TII.get(SubOp).getSize();
  auto IsGPR = [&](Register Reg) {
    return Reg.isPhysical() && Reg != RISCC::R7 &&
           STI.getGPRClass()->contains(Reg);
  };
  auto IsShift = [&](const MachineInstr &MI) {
    if (MI.getOpcode() != ShiftOp || MI.getNumOperands() != 3 ||
        isPhysicalValueBarrier(MI) || MI.isBundled() || MI.isPosition() ||
        MI.getPreInstrSymbol() || MI.getPostInstrSymbol())
      return false;
    const auto &Dst = MI.getOperand(0);
    const auto &Src = MI.getOperand(1);
    const auto &Amount = MI.getOperand(2);
    return Dst.isReg() && Dst.isDef() && !Dst.isImplicit() &&
           !Dst.getSubReg() && IsGPR(Dst.getReg()) && Src.isReg() &&
           Src.isUse() && !Src.isImplicit() && !Src.isUndef() &&
           !Src.getSubReg() && IsGPR(Src.getReg()) && Amount.isImm() &&
           Amount.getImm() >= 1 &&
           Amount.getImm() <= (STI.hasWideShift() ? 8 : 1);
  };
  auto IsPristine = [&](MCPhysReg Reg) {
    return TRI.isCalleeSavedPhysReg(Reg, MF) &&
           (!MFI.isCalleeSavedInfoValid() ||
            llvm::none_of(MFI.getCalleeSavedInfo(), [&](const auto &Saved) {
              return Saved.getReg() == Reg;
            }));
  };
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    LivePhysRegs Live(TRI);
    Live.addLiveOuts(MBB);
    bool BlockChanged = false;
    for (auto I = MBB.end(); I != MBB.begin();) {
      MachineInstr &Last = *--I;
      if (!IsShift(Last)) {
        Live.stepBackward(Last);
        continue;
      }
      auto First = I;
      const Register Dst = Last.getOperand(0).getReg();
      unsigned Amount = Last.getOperand(2).getImm();
      unsigned Bytes = TII.getInstSizeInBytes(Last);
      while (Amount < Bits - 1 && First != MBB.begin()) {
        auto Prev = std::prev(First);
        if (!IsShift(*Prev) ||
            Prev->getOperand(0).getReg() != First->getOperand(1).getReg())
          break;
        Register Temp = Prev->getOperand(0).getReg();
        // Earlier results can change registers, but no intermediate value
        // other than the final destination may remain observable afterwards.
        if (Temp != Dst && Live.contains(Temp))
          break;
        Amount += Prev->getOperand(2).getImm();
        Bytes += TII.getInstSizeInBytes(*Prev);
        First = Prev;
      }
      MCPhysReg Scratch = 0;
      const Register Src = First->getOperand(1).getReg();
      if (Amount >= Bits - 1 && Bytes > NewBytes)
        for (MCPhysReg Reg : *STI.getGPRClass())
          if (Reg != Src && Reg != Dst && Live.available(MRI, Reg) &&
              !IsPristine(Reg)) {
            Scratch = Reg;
            break;
          }
      if (!Scratch) {
        Live.stepBackward(Last);
        continue;
      }
      const DebugLoc DL = First->getDebugLoc();
      auto Zero = BuildMI(MBB, First, DL, TII.get(ZeroOp), Scratch).addImm(0);
      auto Less = BuildMI(MBB, First, DL, TII.get(LessOp), Dst)
                      .addReg(Src)
                      .addReg(Scratch);
      auto Negate = BuildMI(MBB, First, DL, TII.get(SubOp), Dst)
                        .addReg(Scratch)
                        .addReg(Dst);
      MBB.erase(First, std::next(I));
      I = Zero->getIterator();
      Live.stepBackward(*Negate);
      Live.stepBackward(*Less);
      Live.stepBackward(*Zero);
      BlockChanged = true;
    }
    if (BlockChanged) {
      recomputeLivenessFlags(MBB);
      Changed = true;
    }
  }
  return Changed;
}

// Return directly through an available GPR instead of copying its address to
// an S register first. Only call-clobbered, ordinary S registers may lose the
// write: S0/S1 have runtime roles, and S5/S6 must retain their restored values.
// Do this after frame lowering, when all epilogue scratch clobbers are known.
static bool foldReturnTransfers(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  if (STI.isNano())
    return false;
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    if (!MBB.succ_empty())
      continue;
    auto Ret = MBB.getLastNonDebugInstr();
    if (Ret == MBB.end() || Ret->isBundled())
      continue;
    Register Link;
    switch (Ret->getOpcode()) {
    default:
      continue;
    case RISCC::RETS:
      Link = RISCC::S7;
      break;
    case RISCC::LINK_S3_RET:
      Link = RISCC::S3;
      break;
    case RISCC::RET:
      Link = Ret->getOperand(0).getReg();
      break;
    }
    if (Link != RISCC::S2 && Link != RISCC::S3 && Link != RISCC::S4 &&
        Link != RISCC::S7)
      continue;
    for (auto I = Ret; I != MBB.begin();) {
      MachineInstr &MI = *--I;
      // Do not change the address's location across an observable boundary,
      // or remove a transfer whose S-register result has another use.
      if (MI.isDebugInstr() || MI.isCFIInstruction() || MI.isCall() ||
          MI.isTerminator() || MI.isInlineAsm() || MI.isBundled() ||
          MI.hasUnmodeledSideEffects() || MI.readsRegister(Link, &TRI))
        break;
      if (!MI.modifiesRegister(Link, &TRI))
        continue;
      if (MI.getOpcode() != RISCC::MTS || MI.getNumOperands() != 2 ||
          MI.getOperand(0).getReg() != Link || MI.getOperand(1).isUndef() ||
          MI.getOperand(1).getSubReg())
        break;
      Register Target = MI.getOperand(1).getReg();
      if (!STI.getGPRClass()->contains(Target) ||
          llvm::any_of(make_range(std::next(I), Ret),
                       [&](const MachineInstr &Between) {
                         return Between.modifiesRegister(Target, &TRI);
                       }))
        break;
      auto NewRet =
          BuildMI(MBB, Ret, Ret->getDebugLoc(), TII.get(RISCC::RET_REG))
              .addReg(Target);
      NewRet->setFlags(Ret->getFlags());
      for (const MachineOperand &MO : Ret->implicit_operands())
        if (!MO.isReg() || MO.getReg() != Link)
          NewRet.add(MO);
      Ret->eraseFromParent();
      MI.eraseFromParent();
      recomputeLivenessFlags(MBB);
      Changed = true;
      break;
    }
  }
  return Changed;
}

static bool optimizePhysicalRegisters(MachineFunction &MF) {
  bool Changed = EnableBranchCopyPaths && coalesceLoopBranchCopyPaths(MF);
  Changed |= EnableLoopBranchRegister && keepLoopBranchValueInR0(MF);
  Changed |= EnableInvariantCopies && hoistInvariantCopies(MF);
  Changed |= enableLocalCopies(MF) && coalesceRISCCLocalCopies(MF);
  Changed |= EnableAffineValues && foldPhysicalImmediateChains(MF);
  Changed |= foldR0ImmediateAdds(MF);
  Changed |= removeRepeatedPhysicalValues(MF);
  Changed |= EnableConstantSynthesis && synthesizeUnsharedConstants(MF);
  Changed |= foldReturnTransfers(MF);
  return Changed;
}

class RISCCMachineOptimizeLegacy final : public MachineFunctionPass {
public:
  static char ID;
  RISCCMachineOptimizeLegacy() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override {
    return "RISC-C machine SSA optimizations";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  bool runOnMachineFunction(MachineFunction &MF) override {
    bool Changed = removeRedundantExtensions(MF);
    Changed |= removeUnusedUpperBits(MF);
    Changed |= foldAddressOffsets(MF);
    return delayAddsAcrossCalls(MF) || Changed;
  }
};

class RISCCPostRAOptimizeLegacy final : public MachineFunctionPass {
public:
  static char ID;
  RISCCPostRAOptimizeLegacy() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override {
    return "RISC-C physical-register optimizations";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  bool runOnMachineFunction(MachineFunction &MF) override {
    return optimizePhysicalRegisters(MF);
  }
};

class RISCCReturnOptimizeLegacy final : public MachineFunctionPass {
public:
  static char ID;
  RISCCReturnOptimizeLegacy() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override {
    return "RISC-C return transfer optimizations";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  bool runOnMachineFunction(MachineFunction &MF) override {
    bool Changed = enableLocalCopies(MF) && MF.getFunction().hasOptSize() &&
                   coalesceRISCCLocalCopies(MF);
    Changed |= removeSignBranchShifts(MF);
    Changed |= optimizeLateSignMasks(MF);
    return foldReturnTransfers(MF) || Changed;
  }
};
} // namespace
bool llvm::enableRISCCConstantBranches() { return EnableConstantBranches; }

char RISCCMachineOptimizeLegacy::ID = 0;
INITIALIZE_PASS(RISCCMachineOptimizeLegacy, DEBUG_TYPE,
                "RISC-C machine SSA optimizations", false, false)
FunctionPass *llvm::createRISCCMachineOptimizeLegacyPass() {
  return new RISCCMachineOptimizeLegacy();
}
PreservedAnalyses
RISCCMachineOptimizePass::run(MachineFunction &MF,
                              MachineFunctionAnalysisManager &) {
  bool Changed = removeRedundantExtensions(MF);
  Changed |= removeUnusedUpperBits(MF);
  Changed |= foldAddressOffsets(MF);
  Changed |= delayAddsAcrossCalls(MF);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

char RISCCPostRAOptimizeLegacy::ID = 0;
INITIALIZE_PASS(RISCCPostRAOptimizeLegacy, "riscc-postra-optimize",
                "RISC-C physical-register optimizations", false, false)
FunctionPass *llvm::createRISCCPostRAOptimizeLegacyPass() {
  return new RISCCPostRAOptimizeLegacy();
}
PreservedAnalyses
RISCCPostRAOptimizePass::run(MachineFunction &MF,
                             MachineFunctionAnalysisManager &) {
  return optimizePhysicalRegisters(MF) ? PreservedAnalyses::none()
                                       : PreservedAnalyses::all();
}

char RISCCReturnOptimizeLegacy::ID = 0;
INITIALIZE_PASS(RISCCReturnOptimizeLegacy, "riscc-return-optimize",
                "RISC-C return transfer optimizations", false, false)
FunctionPass *llvm::createRISCCReturnOptimizeLegacyPass() {
  return new RISCCReturnOptimizeLegacy();
}
PreservedAnalyses
RISCCReturnOptimizePass::run(MachineFunction &MF,
                             MachineFunctionAnalysisManager &) {
  bool Changed = enableLocalCopies(MF) && MF.getFunction().hasOptSize() &&
                 coalesceRISCCLocalCopies(MF);
  Changed |= removeSignBranchShifts(MF);
  Changed |= optimizeLateSignMasks(MF);
  Changed |= foldReturnTransfers(MF);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
