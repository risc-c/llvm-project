//===-- RISCCMachineScheduler.cpp - Load scheduling -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCInstrInfo.h"
#include "RISCCSubtarget.h"
#include "RISCCTargetMachine.h"
#include "llvm/CodeGen/AntiDepBreaker.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/RegisterClassInfo.h"

using namespace llvm;

namespace {
// Fill branch gaps with work from a common successor. Only move operations
// needed on both paths, with no intervening use or change of their operands.
// This includes private register restores and independent immediate updates.
// No instruction is duplicated or speculated.
static void hoistCommonWork(MachineBasicBlock &MBB) {
  auto &MF = *MBB.getParent();
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TRI = *STI.getRegisterInfo();
  if (!STI.hasEarlyBranches() || MBB.succ_size() != 2)
    return;
  auto Branch = MBB.getFirstTerminator();
  if (Branch == MBB.end() || !Branch->isConditionalBranch() ||
      !Branch->readsRegister(RISCC::R0, &TRI))
    return;
  unsigned Gap = 0, Needed = 0;
  for (auto I = Branch; I != MBB.begin();) {
    MachineInstr &MI = *--I;
    if (MI.isDebugInstr())
      continue;
    if (MI.modifiesRegister(RISCC::R0, &TRI)) {
      Needed = MI.mayLoad() ? 2 : 1;
      break;
    }
    if (++Gap == 2)
      return;
  }
  if (Gap >= Needed)
    return;

  // Recognize a small closed diamond, including a direct edge to its join.
  // Unique predecessors prevent removing work needed by an outside path.
  auto Chain = [&](MachineBasicBlock *BB) {
    SmallVector<MachineBasicBlock *, 4> Blocks;
    while (BB != &MBB && Blocks.size() < 4 && !is_contained(Blocks, BB)) {
      Blocks.push_back(BB);
      if (BB->succ_size() != 1)
        break;
      BB = *BB->succ_begin();
    }
    return Blocks;
  };
  auto A = Chain(*MBB.succ_begin());
  auto B = Chain(*std::next(MBB.succ_begin()));
  MachineBasicBlock *Join = nullptr;
  for (auto *BB : A)
    if (is_contained(B, BB)) {
      Join = BB;
      break;
    }
  if (!Join || Join->pred_size() != 2)
    return;
  SmallVector<MachineBasicBlock *, 6> Arms;
  for (const auto &Path : {A, B}) {
    MachineBasicBlock *Previous = &MBB;
    for (auto *BB : Path) {
      if (BB == Join) {
        if (!Join->isPredecessor(Previous))
          return;
        break;
      }
      if (BB->pred_size() != 1 || *BB->pred_begin() != Previous)
        return;
      Arms.push_back(BB);
      Previous = BB;
    }
  }

  bool Changed = false;
  for (auto I = Join->begin(); I != Join->end() && Gap < Needed;) {
    MachineInstr &Work = *I++;
    const bool IsAdd = Work.getOpcode() == RISCC::ADDI ||
                       Work.getOpcode() == RISCC::ADDI32;
    const bool IsLoad = Work.getOpcode() == RISCC::LD ||
                        Work.getOpcode() == RISCC::LD32;
    if ((!IsAdd && !IsLoad) || Work.getFlag(MachineInstr::FrameSetup) ||
        Work.getFlag(MachineInstr::FrameDestroy))
      continue;
    Register Dst = Work.getOperand(0).getReg();
    Register Base = Work.getOperand(1).getReg();
    if (Dst == RISCC::R0 || Dst == RISCC::R7)
      continue;
    const FixedStackPseudoSourceValue *Slot = nullptr;
    if (IsAdd) {
      if (Base != Dst)
        continue;
    } else {
      if (!Join->isReturnBlock() || Base != RISCC::R7 ||
          Work.memoperands().size() != 1 ||
          !Work.memoperands()[0]->isUnordered())
        continue;
      Slot = dyn_cast_or_null<FixedStackPseudoSourceValue>(
          Work.memoperands()[0]->getPseudoValue());
      if (!Slot || Work.memoperands()[0]->getOffset() != 0 ||
          !llvm::any_of(MF.getFrameInfo().getCalleeSavedInfo(),
                       [&](const CalleeSavedInfo &Save) {
                         return !Save.isSpilledToReg() &&
                                Save.getReg() == Dst.asMCReg() &&
                                Save.getFrameIdx() == Slot->getFrameIndex();
                       }))
        continue;
    }
    auto CanCross = [&](const MachineInstr &MI) {
      // The call mask must explicitly establish preservation of an update's
      // register; an unmodelled call cannot be crossed.
      if (MI.isCall() && !llvm::any_of(MI.operands(), [](const auto &MO) {
            return MO.isRegMask();
          }))
        return false;
      if ((IsLoad && MI.isCall()) || MI.isInlineAsm() || MI.isCFIInstruction() ||
          (!MI.isCall() && MI.hasUnmodeledSideEffects()) ||
          MI.readsRegister(Dst, &TRI) ||
          MI.modifiesRegister(Dst, &TRI) || MI.modifiesRegister(Base, &TRI))
        return false;
      // Compiler spill slots do not alias program-visible memory. A spill
      // to the same private slot must nevertheless remain before its load.
      if (IsLoad && MI.mayStore())
        for (const auto *MMO : MI.memoperands())
          if (MMO->getPseudoValue() == Slot)
            return false;
      return IsAdd || !MI.mayStore() || !MI.memoperands_empty();
    };
    if (!llvm::all_of(make_range(Branch, MBB.end()), CanCross) ||
        !llvm::all_of(make_range(Join->instr_begin(), Work.getIterator()),
                      CanCross) ||
        !llvm::all_of(Arms, [&](auto *BB) { return llvm::all_of(*BB, CanCross); }))
      continue;
    MBB.splice(Branch, Join, Work.getIterator());
    ++Gap;
    Changed = true;
  }
  if (Changed) {
    recomputeLiveIns(*Join);
    for (auto *BB : llvm::reverse(Arms))
      recomputeLiveIns(*BB);
    recomputeLiveIns(MBB);
    recomputeLivenessFlags(*Join);
    for (auto *BB : Arms)
      recomputeLivenessFlags(*BB);
    recomputeLivenessFlags(MBB);
  }
}

// (a - b) + C can evaluate a + C while the second load completes. Do this
// after allocation: the dying first operand supplies the temporary, so the
// rewrite adds neither an instruction nor register pressure. Both loads stay
// in place, including volatile accesses.
static void reassociateLoadedSubtracts(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator Begin,
                                      MachineBasicBlock::iterator End) {
  const auto &STI = MBB.getParent()->getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  for (auto I = Begin; I != End; ++I) {
    auto J = I;
    MachineInstr *Seq[4];
    unsigned Count = 0;
    for (; J != End && Count != 4; ++J)
      Seq[Count++] = &*J;
    if (Count != 4)
      break;
    MachineInstr &LoadA = *Seq[0], &LoadB = *Seq[1];
    MachineInstr &Sub = *Seq[2], &Add = *Seq[3];
    unsigned SubOp = STI.isRC32() ? RISCC::SUB32 : RISCC::SUB;
    unsigned AddOp = STI.isRC32() ? RISCC::ADDI32 : RISCC::ADDI;
    unsigned LoadOp = STI.isRC32() ? RISCC::LD32 : RISCC::LD;
    if (LoadA.getOpcode() != LoadOp || LoadB.getOpcode() != LoadOp ||
        Sub.getOpcode() != SubOp || Add.getOpcode() != AddOp ||
        !Sub.getOperand(1).isKill() || !Add.getOperand(2).isImm())
      continue;
    Register Dst = Sub.getOperand(0).getReg();
    Register A = Sub.getOperand(1).getReg(), B = Sub.getOperand(2).getReg();
    if (A == B || A == RISCC::R7 || Dst == RISCC::R7 ||
        LoadA.getOperand(0).getReg() != A ||
        LoadB.getOperand(0).getReg() != B ||
        Add.getOperand(0).getReg() != Dst ||
        Add.getOperand(1).getReg() != Dst)
      continue;

    int64_t Imm = Add.getOperand(2).getImm();
    bool KillB = Sub.getOperand(2).isKill();
    Sub.setDesc(TII.get(AddOp));
    Sub.getOperand(0).setReg(A);
    Sub.getOperand(2).ChangeToImmediate(Imm);
    Sub.tieOperands(0, 1);
    Add.untieRegOperand(1);
    Add.setDesc(TII.get(SubOp));
    Add.getOperand(1).setReg(A);
    Add.getOperand(1).setIsKill(true);
    Add.getOperand(2).ChangeToRegister(B, false, false, KillB);
    // Modular reassociation does not preserve intermediate no-wrap flags.
    Sub.setFlags(0);
    Add.setFlags(0);
  }
}

// Rebase stores on an updated pointer, allowing the ADDI to fill an earlier
// load-use gap. Loads keep their original order and addresses, including
// volatile accesses. Stop at any other use or definition of the pointer.
static void advancePointerUpdates(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator Begin,
                                  MachineBasicBlock::iterator End) {
  const auto &STI = MBB.getParent()->getSubtarget<RISCCSubtarget>();
  const auto &TRI = *STI.getRegisterInfo();
  for (auto I = Begin; I != End;) {
    MachineInstr &Add = *I++;
    if (Add.getOpcode() != RISCC::ADDI && Add.getOpcode() != RISCC::ADDI32)
      continue;
    Register Base = Add.getOperand(0).getReg();
    if (Base == RISCC::R7 || Base != Add.getOperand(1).getReg() ||
        Add.getFlag(MachineInstr::FrameSetup) ||
        Add.getFlag(MachineInstr::FrameDestroy))
      continue;

    int64_t Delta = Add.getOperand(2).getImm();
    SmallVector<MachineInstr *, 4> Stores;
    auto Position = Add.getIterator();
    // Keep the region's first instruction in place: scheduler iterators and
    // backwards liveness tracking refer to that boundary.
    while (Position != Begin && Position != std::next(Begin)) {
      auto Prev = std::prev(Position);
      MachineInstr &MI = *Prev;
      if (MI.isDebugInstr() || MI.isCall() || MI.isTerminator() ||
          MI.hasUnmodeledSideEffects() || MI.modifiesRegister(Base, &TRI))
        break;
      if (MI.readsRegister(Base, &TRI)) {
        if ((MI.getOpcode() != RISCC::ST && MI.getOpcode() != RISCC::ST32) ||
            !MI.getOperand(1).isReg() || MI.getOperand(1).getReg() != Base ||
            MI.getOperand(0).getReg() == Base || !MI.getOperand(2).isImm() ||
            !STI.isLegalWordOffset(MI.getOperand(2).getImm() - Delta))
          break;
        Stores.push_back(&MI);
      }
      Position = Prev;
    }
    if (Stores.empty())
      continue;
    for (MachineInstr *Store : Stores) {
      Store->getOperand(2).setImm(Store->getOperand(2).getImm() - Delta);
      Store->getOperand(1).setIsKill(false);
    }
    Add.getOperand(1).setIsKill(false);
    MBB.splice(Position, &MBB, Add.getIterator());
  }
}

// Rematerialized literals often reuse the same temporary for successive
// load/store pairs. Rename those false dependencies before scheduling, so an
// independent load can fill the previous load's latency without extra spills.
class RISCCPostScheduleDAG final : public ScheduleDAGMI {
  RegisterClassInfo RegClassInfo;
  std::unique_ptr<AntiDepBreaker> AntiDeps;
  MachineBasicBlock::iterator Cursor;
  unsigned Remaining = 0;
  unsigned PreviousEnd = 0;

public:
  explicit RISCCPostScheduleDAG(MachineSchedContext *C)
      : ScheduleDAGMI(C, std::make_unique<PostGenericScheduler>(C),
                      /*RemoveKillFlags=*/true) {
    RegClassInfo.runOnMachineFunction(*C->MF);
    AntiDeps.reset(createCriticalAntiDepBreaker(*C->MF, RegClassInfo));
  }

  void startBlock(MachineBasicBlock *MBB) override {
    hoistCommonWork(*MBB);
    ScheduleDAGMI::startBlock(MBB);
    AntiDeps->StartBlock(MBB);
    Cursor = MBB->end();
    Remaining = MBB->size();
    PreviousEnd = Remaining;
  }

  // Liveness is tracked backwards across calls, terminators and any regions
  // too small to schedule. Each instruction is observed exactly once.
  void enterRegion(MachineBasicBlock *MBB, MachineBasicBlock::iterator Begin,
                   MachineBasicBlock::iterator End, unsigned Count) override {
    while (Cursor != End)
      AntiDeps->Observe(*--Cursor, --Remaining, PreviousEnd);
    PreviousEnd = Remaining;
    ScheduleDAGMI::enterRegion(MBB, Begin, End, Count);
  }

  void schedule() override {
    unsigned Count = std::distance(RegionBegin, RegionEnd);
    reassociateLoadedSubtracts(*BB, RegionBegin, RegionEnd);
    advancePointerUpdates(*BB, RegionBegin, RegionEnd);
    buildSchedGraph(AA);
    AntiDeps->BreakAntiDependencies(SUnits, RegionBegin, RegionEnd, Remaining,
                                    DbgValues);
    // The normal scheduler rebuilds the graph with the renamed registers.
    ScheduleDAGMI::schedule();
    Cursor = RegionBegin;
    Remaining -= Count;
  }

  void finishBlock() override {
    AntiDeps->FinishBlock();
    ScheduleDAGMI::finishBlock();
  }
};
} // namespace

ScheduleDAGInstrs *
RISCCTargetMachine::createPostMachineScheduler(MachineSchedContext *C) const {
  return new RISCCPostScheduleDAG(C);
}
