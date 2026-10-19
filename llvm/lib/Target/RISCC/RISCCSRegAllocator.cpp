//===-- RISCCSRegAllocator.cpp - Plan use of the S-register cache --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RISC-C S registers are a small software-managed cache. They cannot
// participate in ordinary register allocation because ALU instructions cannot
// use them directly. After register and stack-slot coloring, this pass ranks
// the remaining spill slots and the function's entry values, then assigns the
// cache registers greedily. Spills with disjoint lifetimes can share a
// register; only calls crossed by a live spill need to preserve its register.
//
//===----------------------------------------------------------------------===//

#include "RISCCSRegAllocator.h"
#include "RISCCFrameLowering.h"
#include "RISCCInstrInfo.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"
#include <array>

using namespace llvm;

#define DEBUG_TYPE "riscc-sreg-allocator"

namespace {

static cl::opt<bool> EnableLoopSpillHoist(
    "riscc-loop-spill-hoist", cl::Hidden, cl::init(true),
    cl::desc("Borrow an unused live-through GPR for an invariant loop spill"));
static cl::opt<bool> EnableMultiBlockSpillHoist(
    "riscc-multiblock-spill-hoist", cl::Hidden, cl::init(false),
    cl::desc("Extend invariant spill GPR borrowing to multi-block loops"));
static cl::opt<bool> EnableLoopSpillPromotion(
    "riscc-loop-spill-promotion", cl::Hidden, cl::init(true),
    cl::desc("Keep private loop spills in their local working register"));
static cl::opt<bool> EnableLoopInvariantSpills(
    "riscc-loop-invariant-spills", cl::Hidden, cl::init(true),
    cl::desc(
        "Keep invariant spills resident across disjoint local GPR values"));

enum class CandidateKind { Spill, CalleeSave, ReturnAddress };

struct Candidate {
  uint64_t Weight;
  CandidateKind Kind;
  int FrameIndex = -1;
  MCRegister Reg;
};

static bool referencesFrameIndex(const MachineInstr &MI, int FI) {
  return llvm::any_of(MI.operands(), [FI](const MachineOperand &MO) {
    return MO.isFI() && MO.getIndex() == FI;
  });
}

static bool isSimpleSpillAccess(const MachineInstr &MI, int FI) {
  return (MI.getOpcode() == RISCC::ST || MI.getOpcode() == RISCC::LD ||
          MI.getOpcode() == RISCC::ST32 || MI.getOpcode() == RISCC::LD32) &&
         MI.getNumOperands() >= 3 && MI.getOperand(1).isFI() &&
         MI.getOperand(1).getIndex() == FI && MI.getOperand(2).isImm() &&
         MI.getOperand(2).getImm() == 0;
}

// A register unused inside a hot loop can be less valuable there than an
// invariant spill. Save that live-through value at the entry, borrow its GPR
// for the spill, and restore it on the dedicated exit. This refines an already
// allocated loop without changing its CFG or memory ordering. Reload uses
// must remain local to their block; cross-block value renaming needs edge
// copies and is deliberately left to the register allocator.
static bool hoistLoopSpill(MachineFunction &MF,
                           const MachineBlockFrequencyInfo &MBFI,
                           const MachineLoopInfo &MLI) {
  if (MF.getFunction().hasOptSize())
    return false;
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const unsigned LoadOp = TII.getNativeOpcode(RISCC::LD);
  bool Changed = false;
  for (MachineLoop *Loop : MLI.getLoopsInPreorder()) {
    // The extra setup can improve the loop while worsening whole-function
    // layout and throughput. Keep the broader policy explicitly opt-in.
    if (Loop->getNumBlocks() != 1 && !EnableMultiBlockSpillHoist)
      continue;
    MachineBasicBlock *Entry = Loop->getLoopPreheader();
    MachineBasicBlock *Exit = Loop->getExitBlock();
    if (!Entry || !Exit || Entry->isEHPad() || Entry->hasAddressTaken() ||
        Exit->isEHPad() || Exit->hasAddressTaken() ||
        !llvm::all_of(Exit->predecessors(),
                      [&](auto *BB) { return Loop->contains(BB); }))
      continue;
    if (llvm::any_of(Loop->blocks(), [](auto *BB) {
          return BB->isEHPad() || BB->hasAddressTaken() ||
                 llvm::any_of(*BB, [](const MachineInstr &MI) {
                   return MI.isCall() || MI.isInlineAsm() || MI.isBundled() ||
                          MI.hasUnmodeledSideEffects() ||
                          MI.isCFIInstruction() ||
                          MI.getFlag(MachineInstr::FrameSetup) ||
                          MI.getFlag(MachineInstr::FrameDestroy);
                 });
        }))
      continue;
    Register Borrow;
    for (MCPhysReg Reg : *STI.getGPRClass()) {
      if (Reg == RISCC::R7 || !Loop->getHeader()->isLiveIn(Reg) ||
          !Exit->isLiveIn(Reg))
        continue;
      auto Mentions = [&](const MachineInstr &MI) {
        return MI.readsRegister(Reg, &TRI) || MI.modifiesRegister(Reg, &TRI);
      };
      if (llvm::all_of(
              Loop->blocks(),
              [&](auto *BB) { return llvm::none_of(*BB, Mentions); }) &&
          llvm::none_of(make_range(Entry->getFirstTerminator(), Entry->end()),
                        Mentions)) {
        Borrow = Reg;
        break;
      }
    }
    if (!Borrow)
      continue;
    SmallVector<int, 4> Slots;
    SmallVector<MachineInstr *> LoopInstructions;
    for (MachineBasicBlock *BB : Loop->blocks())
      for (MachineInstr &MI : *BB) {
        LoopInstructions.push_back(&MI);
        if (MI.getOpcode() == LoadOp && MI.getOperand(1).isFI() &&
            MFI.isSpillSlotObjectIndex(MI.getOperand(1).getIndex()) &&
            !llvm::is_contained(Slots, MI.getOperand(1).getIndex()))
          Slots.push_back(MI.getOperand(1).getIndex());
      }
    for (int FI : Slots) {
      // A spill object is private only while its address never escapes. Do
      // not hoist a load if another reference materializes that address or
      // accesses a differently sized part of the object.
      bool Private = llvm::all_of(MF, [&](const MachineBasicBlock &MBB) {
        return llvm::all_of(MBB, [&](const MachineInstr &MI) {
          return !referencesFrameIndex(MI, FI) ||
                 (isSimpleSpillAccess(MI, FI) &&
                  (MI.getOpcode() == LoadOp ||
                   MI.getOpcode() == TII.getNativeOpcode(RISCC::ST)));
        });
      });
      if (!Private)
        continue;
      SmallVector<MachineInstr *> Loads;
      SmallVector<MachineOperand *> Uses;
      SmallVector<Register> ExitCopies;
      bool Safe = true;
      uint64_t Saved = 0;
      for (MachineInstr *Instruction : LoopInstructions) {
        MachineInstr &MI = *Instruction;
        MachineBasicBlock &BB = *MI.getParent();
        if (!referencesFrameIndex(MI, FI))
          continue;
        if (MI.getOpcode() != LoadOp || !isSimpleSpillAccess(MI, FI) ||
            MI.hasOrderedMemoryRef() || MI.getNumOperands() != 3) {
          Safe = false;
          break;
        }
        Register Dst = MI.getOperand(0).getReg();
        if (Dst == RISCC::R7 || BB.isLiveIn(Dst)) {
          Safe = false;
          break;
        }
        Loads.push_back(&MI);
        bool Redefined = false;
        for (auto I = std::next(MI.getIterator()); I != BB.end(); ++I) {
          for (MachineOperand &MO : I->operands()) {
            if (!MO.isReg() || !MO.isUse() || MO.getReg() != Dst)
              continue;
            if (MO.isImplicit() || MO.isTied() || MO.getSubReg() ||
                MO.isUndef() || I->isDebugInstr()) {
              Safe = false;
              break;
            }
            Uses.push_back(&MO);
          }
          if (!Safe || I->modifiesRegister(Dst, &TRI)) {
            Redefined = true;
            break;
          }
        }
        if (!Safe)
          break;
        if (!Redefined && llvm::any_of(BB.successors(), [&](auto *Succ) {
              return Succ->isLiveIn(Dst);
            })) {
          // A single-block loop establishes the reload value on every exit.
          // In a multi-block loop another path may carry a different value.
          if (Loop->getNumBlocks() != 1) {
            Safe = false;
            break;
          }
          if (!llvm::is_contained(ExitCopies, Dst))
            ExitCopies.push_back(Dst);
        }
        Saved = SaturatingAdd(Saved, MBFI.getBlockFreq(&BB).getFrequency());
      }
      if (!Safe || Loads.empty())
        continue;
      uint64_t Added = SaturatingAdd(
          SaturatingMultiply(MBFI.getBlockFreq(Entry).getFrequency(),
                             uint64_t(2)),
          SaturatingMultiply(MBFI.getBlockFreq(Exit).getFrequency(),
                             uint64_t(1 + ExitCopies.size())));
      if (Saved <= Added)
        continue;
      int SavedFI = MFI.CreateStackObject(STI.getSlotSize(),
                                          STI.getStackAlignment(), true);
      auto Insert = Entry->getFirstTerminator();
      TII.storeRegToStackSlot(*Entry, Insert, Borrow, false, SavedFI,
                              STI.getGPRClass(), Register(),
                              MachineInstr::NoFlags);
      TII.loadRegFromStackSlot(*Entry, Insert, Borrow, FI, STI.getGPRClass(),
                               Register(), 0, MachineInstr::NoFlags);
      auto Restore = Exit->getFirstNonPHI();
      for (Register Dst : ExitCopies)
        BuildMI(*Exit, Restore, DebugLoc(), TII.get(TargetOpcode::COPY), Dst)
            .addReg(Borrow);
      TII.loadRegFromStackSlot(*Exit, Restore, Borrow, SavedFI,
                               STI.getGPRClass(), Register(), 0,
                               MachineInstr::NoFlags);
      for (MachineOperand *Use : Uses) {
        Use->setReg(Borrow);
        Use->setIsKill(false);
      }
      for (MachineInstr *Load : Loads)
        Load->eraseFromParent();
      for (MachineBasicBlock *BB : Loop->blocks())
        if (!BB->isLiveIn(Borrow))
          BB->addLiveIn(Borrow);
      for (MachineBasicBlock &MBB : MF)
        recomputeLivenessFlags(MBB);
      Changed = true;
      // The borrowed GPR is no longer unused. Revisit another loop, not a
      // second spill using the same register in this loop.
      break;
    }
  }
  return Changed;
}

// Keep an invariant private spill in its reload register across a loop nest.
// The register may also hold other, short-lived values between reloads. Those
// values can share another GPR when their live ranges do not overlap its own
// non-invariant values. Copies of the invariant into that GPR need no storage:
// their uses can read the resident register directly. This is local physical
// coalescing, not an assumption that a live-through outer-loop value is dead.
static bool cacheLoopInvariantSpills(MachineFunction &MF,
                                     const MachineBlockFrequencyInfo &MBFI,
                                     const MachineLoopInfo &MLI) {
  if (MF.getFunction().hasOptNone())
    return false;
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  auto &MFI = MF.getFrameInfo();
  auto &MRI = MF.getRegInfo();
  const unsigned Load = TII.getNativeOpcode(RISCC::LD);
  const unsigned Store = TII.getNativeOpcode(RISCC::ST);
  // A union at a CFG merge must retain both possibilities. Treating a mixed
  // value as merely non-invariant would rename different incoming values to
  // different physical locations without inserting edge copies.
  constexpr unsigned Other = 1, Invariant = 2, Mixed = Other | Invariant;
  using State = std::array<unsigned, 2>;
  bool Changed = false;
  for (MachineLoop *Loop : MLI.getLoopsInPreorder()) {
    auto *Entry = Loop->getLoopPreheader();
    auto *Exit = Loop->getExitBlock();
    if (!Entry || !Exit || Entry->isEHPad() || Entry->hasAddressTaken() ||
        Exit->isEHPad() || Exit->hasAddressTaken() ||
        !llvm::all_of(Exit->predecessors(),
                      [&](auto *BB) { return Loop->contains(BB); }) ||
        llvm::any_of(Loop->blocks(), [](auto *BB) {
          return BB->isEHPad() || BB->hasAddressTaken() ||
                 llvm::any_of(*BB, [](const MachineInstr &MI) {
                   return MI.isCall() || MI.isInlineAsm() || MI.isBundled() ||
                          MI.isCFIInstruction() ||
                          MI.hasUnmodeledSideEffects() ||
                          MI.getFlag(MachineInstr::FrameSetup) ||
                          MI.getFlag(MachineInstr::FrameDestroy);
                 });
        }))
      continue;

    SmallVector<std::pair<int, MCRegister>, 4> Candidates;
    for (MachineBasicBlock *BB : Loop->blocks())
      for (MachineInstr &MI : *BB)
        if (MI.getOpcode() == Load && MI.getOperand(1).isFI()) {
          std::pair<int, MCRegister> C{MI.getOperand(1).getIndex(),
                                       MI.getOperand(0).getReg().asMCReg()};
          if (!llvm::is_contained(Candidates, C))
            Candidates.push_back(C);
        }
    for (auto [FI, Reg] : Candidates) {
      if (FI < 0 || !MFI.isSpillSlotObjectIndex(FI) ||
          MFI.getObjectSize(FI) != STI.getSlotSize() || Reg == RISCC::R0 ||
          Reg == RISCC::R7 || !STI.getGPRClass()->contains(Reg) ||
          llvm::any_of(
              MF.getInStackSlotVariableDbgInfo(),
              [FI](const auto &Var) { return Var.getStackSlot() == FI; }) ||
          !llvm::all_of(
              MF,
              [&](const MachineBasicBlock &BB) {
                return llvm::all_of(BB, [&](const MachineInstr &MI) {
                  return !referencesFrameIndex(MI, FI) ||
                         (!MI.isDebugInstr() && isSimpleSpillAccess(MI, FI) &&
                          (MI.getOpcode() == Load ||
                           (MI.getOpcode() == Store && !Loop->contains(&BB))) &&
                          MI.getNumOperands() == 3 &&
                          !MI.hasOrderedMemoryRef());
                });
              }))
        continue;

      bool Applied = false;
      for (MCRegister Spare :
           {RISCC::R1, RISCC::R2, RISCC::R3, RISCC::R4, RISCC::R5, RISCC::R6}) {
        if (Spare == Reg ||
            ((Spare == RISCC::R4 || Spare == RISCC::R5 || Spare == RISCC::R6) &&
             !MRI.isPhysRegUsed(Spare)))
          continue;
        const std::array<MCRegister, 2> Regs{Reg, Spare};
        auto Transfer = [&](const MachineInstr &MI, State Before) {
          State After = Before;
          if (MI.isDebugInstr())
            return After;
          for (unsigned I = 0; I != 2; ++I) {
            if (!MI.modifiesRegister(Regs[I], &TRI))
              continue;
            After[I] = Other;
            if (MI.getOpcode() == Load && isSimpleSpillAccess(MI, FI))
              After[I] = Invariant;
            else if (MI.isCopy() && MI.getNumOperands() == 2 &&
                     !MI.getOperand(1).isUndef() &&
                     !MI.getOperand(1).getSubReg()) {
              for (unsigned J = 0; J != 2; ++J)
                if (MI.getOperand(1).getReg() == Regs[J])
                  After[I] = Before[J];
            }
          }
          return After;
        };
        SmallVector<State, 16> In(MF.getNumBlockIDs()), Out(In.size());
        bool Again;
        do {
          Again = false;
          for (MachineBasicBlock *BB : Loop->blocks()) {
            State S{};
            for (MachineBasicBlock *Pred : BB->predecessors()) {
              State P = Loop->contains(Pred) ? Out[Pred->getNumber()]
                                             : State{Other, Other};
              for (unsigned I = 0; I != 2; ++I)
                S[I] |= P[I];
            }
            In[BB->getNumber()] = S;
            for (const MachineInstr &MI : *BB)
              S = Transfer(MI, S);
            if (Out[BB->getNumber()] != S) {
              Out[BB->getNumber()] = S;
              Again = true;
            }
          }
        } while (Again);

        State ExitState{};
        for (MachineBasicBlock *Pred : Exit->predecessors())
          for (unsigned I = 0; I != 2; ++I)
            ExitState[I] |= Out[Pred->getNumber()][I];
        bool SaveEntry = Loop->getHeader()->isLiveIn(Reg);
        bool RestoreReg = Exit->isLiveIn(Reg) && ExitState[0] == Other;
        bool RestoreSpare = Exit->isLiveIn(Spare) && ExitState[1] == Invariant;
        if ((RestoreReg && RestoreSpare) ||
            (Exit->isLiveIn(Reg) && ExitState[0] == Mixed) ||
            (Exit->isLiveIn(Spare) && ExitState[1] == Mixed) ||
            llvm::any_of(make_range(Entry->getFirstTerminator(), Entry->end()),
                         [&](const MachineInstr &MI) {
                           return MI.readsRegister(Reg, &TRI) ||
                                  MI.modifiesRegister(Reg, &TRI) ||
                                  (SaveEntry &&
                                   (MI.readsRegister(Spare, &TRI) ||
                                    MI.modifiesRegister(Spare, &TRI)));
                         }))
          continue;

        SmallVector<MachineInstr *, 4> Remove;
        SmallVector<std::pair<MachineOperand *, MCRegister>, 16> Rename;
        uint64_t Saved = 0;
        bool Safe = true;
        auto CheckLive = [&](State S, bool LiveReg, bool LiveSpare) {
          return !(LiveReg && S[0] == Mixed) && !(LiveSpare && S[1] == Mixed) &&
                 !(LiveReg && S[0] == Other && LiveSpare && S[1] == Other);
        };
        for (MachineBasicBlock *BB : Loop->blocks()) {
          // Record physical liveness after each instruction. Include dead
          // definitions below: coalescing a dead definition must not clobber
          // the other register's still-live value.
          SmallVector<std::array<bool, 2>, 16> LiveAfter;
          LivePhysRegs Live(TRI);
          Live.addLiveOuts(*BB);
          for (const MachineInstr &MI : llvm::reverse(*BB)) {
            LiveAfter.push_back({Live.contains(Reg), Live.contains(Spare)});
            Live.stepBackward(MI);
          }
          State S = In[BB->getNumber()];
          // Live-in lists may conservatively request an entry copy even
          // when Reg is dead before its first reload. That extra definition
          // of Spare must not clobber a genuinely live incoming value.
          if ((BB == Loop->getHeader() && SaveEntry && Live.contains(Spare)) ||
              !CheckLive(S, Live.contains(Reg), Live.contains(Spare))) {
            Safe = false;
            break;
          }
          for (MachineInstr &MI : *BB) {
            auto AfterLive = LiveAfter.pop_back_val();
            State Next = Transfer(MI, S);
            bool DefReg = MI.modifiesRegister(Reg, &TRI);
            bool DefSpare = MI.modifiesRegister(Spare, &TRI);
            if (!CheckLive(Next, AfterLive[0] || DefReg,
                           AfterLive[1] || DefSpare)) {
              Safe = false;
              break;
            }
            bool Erase = (DefReg && Next[0] == Invariant) ||
                         (DefSpare && Next[1] == Invariant);
            if (Erase) {
              assert((MI.isCopy() || MI.getOpcode() == Load) &&
                     "invariant definition must be a spill reload or copy");
              Remove.push_back(&MI);
              Saved =
                  SaturatingAdd(Saved, MBFI.getBlockFreq(BB).getFrequency());
            } else {
              SmallVector<MCRegister, 8> Mapped(MI.getNumOperands());
              for (unsigned N = 0; N != MI.getNumOperands(); ++N) {
                MachineOperand &MO = MI.getOperand(N);
                if (!MO.isReg())
                  continue;
                Mapped[N] = MO.getReg().asMCReg();
                for (unsigned I = 0; I != 2; ++I) {
                  if (MO.getReg() != Regs[I])
                    continue;
                  unsigned Value = MO.isDef() ? Next[I] : S[I];
                  // Early-clobber outputs also interfere with inputs killed
                  // by this instruction, not just its live-out values.
                  if (MI.isDebugInstr() || MO.isEarlyClobber() ||
                      Value == Mixed || !Value) {
                    Safe = false;
                    break;
                  }
                  Mapped[N] = Value == Invariant ? Reg : Spare;
                  if (MO.getReg() != Mapped[N] &&
                      (MO.isImplicit() || MO.getSubReg() || MO.isUndef())) {
                    Safe = false;
                    break;
                  }
                }
              }
              for (unsigned N = 0; Safe && N != MI.getNumOperands(); ++N) {
                MachineOperand &MO = MI.getOperand(N);
                if (!MO.isReg())
                  continue;
                if (MO.isTied() &&
                    Mapped[N] != Mapped[MI.findTiedOperandIdx(N)]) {
                  Safe = false;
                  break;
                }
                if (MO.getReg() != Mapped[N])
                  Rename.emplace_back(&MO, Mapped[N]);
              }
            }
            S = Next;
            if (!Safe)
              break;
          }
          if (!Safe)
            break;
        }
        unsigned AddedInstructions = 1 + SaveEntry + RestoreReg + RestoreSpare;
        uint64_t Added = SaturatingAdd(
            SaturatingMultiply(MBFI.getBlockFreq(Entry).getFrequency(),
                               uint64_t(1 + SaveEntry)),
            SaturatingMultiply(MBFI.getBlockFreq(Exit).getFrequency(),
                               uint64_t(RestoreReg + RestoreSpare)));
        // Require both fewer estimated dynamic instructions and no static
        // instruction growth, including at -Os/-Oz.
        if (!Safe || Saved <= Added || Remove.size() < AddedInstructions)
          continue;

        auto Insert = Entry->getFirstTerminator();
        if (SaveEntry)
          BuildMI(*Entry, Insert, DebugLoc(), TII.get(TargetOpcode::COPY),
                  Spare)
              .addReg(Reg);
        TII.loadRegFromStackSlot(*Entry, Insert, Reg, FI, STI.getGPRClass(),
                                 Register(), 0, MachineInstr::NoFlags);
        Insert = Exit->getFirstNonPHI();
        if (RestoreSpare)
          BuildMI(*Exit, Insert, DebugLoc(), TII.get(TargetOpcode::COPY), Spare)
              .addReg(Reg);
        if (RestoreReg)
          BuildMI(*Exit, Insert, DebugLoc(), TII.get(TargetOpcode::COPY), Reg)
              .addReg(Spare);
        for (auto [MO, NewReg] : Rename)
          MO->setReg(NewReg);
        for (MachineInstr *MI : Remove)
          MI->eraseFromParent();
        for (MachineBasicBlock *BB : Loop->blocks()) {
          State S = In[BB->getNumber()];
          bool NeedsSpare = (BB->isLiveIn(Reg) && S[0] == Other) ||
                            (BB->isLiveIn(Spare) && S[1] == Other);
          BB->removeLiveIn(Reg);
          BB->removeLiveIn(Spare);
          BB->addLiveIn(Reg);
          if (NeedsSpare)
            BB->addLiveIn(Spare);
        }
        if (RestoreReg) {
          Exit->removeLiveIn(Reg);
          if (!Exit->isLiveIn(Spare))
            Exit->addLiveIn(Spare);
        }
        if (RestoreSpare) {
          Exit->removeLiveIn(Spare);
          if (!Exit->isLiveIn(Reg))
            Exit->addLiveIn(Reg);
        }
        for (MachineBasicBlock &BB : MF)
          recomputeLivenessFlags(BB);
        Changed = Applied = true;
        break;
      }
      if (Applied)
        break;
    }
  }
  return Changed;
}

static bool analyzeSpillSlot(const MachineFunction &MF, int FI,
                             const MachineBlockFrequencyInfo &MBFI,
                             uint64_t &Weight) {
  bool HasStore = false;
  bool HasLoad = false;
  Weight = 0;

  for (const MachineBasicBlock &MBB : MF) {
    uint64_t Frequency = MBFI.getBlockFreq(&MBB).getFrequency();
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || !referencesFrameIndex(MI, FI))
        continue;
      if (!isSimpleSpillAccess(MI, FI))
        return false;

      HasStore |= MI.getOpcode() == RISCC::ST || MI.getOpcode() == RISCC::ST32;
      HasLoad |= MI.getOpcode() == RISCC::LD || MI.getOpcode() == RISCC::LD32;
      Weight = SaturatingAdd(Weight, Frequency);
    }
  }
  return HasStore && HasLoad;
}

static void moveSpillToSReg(MachineFunction &MF, int FI, MCRegister SReg,
                            const RISCCInstrInfo &TII) {
  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstr &MI = *I++;
      if (MI.isDebugInstr() && referencesFrameIndex(MI, FI)) {
        // The slot is going away. Tracking the replacement register here
        // would also require updating the variable's live range.
        if (MI.isDebugValue()) {
          for (MachineOperand &MO : MI.debug_operands())
            if (MO.isFI() && MO.getIndex() == FI)
              MO.ChangeToRegister(Register(), false);
        } else
          MI.eraseFromParent();
        continue;
      }
      if (MI.getNumOperands() < 3 || !MI.getOperand(1).isFI() ||
          MI.getOperand(1).getIndex() != FI)
        continue;

      if (MI.getOpcode() == RISCC::ST || MI.getOpcode() == RISCC::ST32)
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCC::MTS), SReg)
            .addReg(MI.getOperand(0).getReg(),
                    getKillRegState(MI.getOperand(0).isKill()));
      else {
        assert((MI.getOpcode() == RISCC::LD || MI.getOpcode() == RISCC::LD32) &&
               "S-register candidate contains a non-spill reference");
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCC::MFS),
                MI.getOperand(0).getReg())
            .addReg(SReg);
      }
      MI.eraseFromParent();
    }
  }
  MF.getFrameInfo().RemoveStackObject(FI);
}

static bool takeRegister(SmallVectorImpl<MCRegister> &Free, MCRegister Reg) {
  auto I = llvm::find(Free, Reg);
  if (I == Free.end())
    return false;
  Free.erase(I);
  return true;
}

static bool isPreservedByAllCalls(const MachineFunction &MF, MCRegister Reg,
                                  const TargetRegisterInfo &TRI) {
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      // Tail calls run after the epilogue has restored our saved values.
      if (!MI.isCall() || MI.isReturn())
        continue;

      bool HasRegMask =
          llvm::any_of(MI.operands(),
                       [](const MachineOperand &MO) { return MO.isRegMask(); });
      if (!HasRegMask || MI.modifiesRegister(Reg, &TRI))
        return false;
    }
  }
  return true;
}

// Stack-slot coloring has already merged compatible slots. Recompute their
// liveness here so call clobbers and S-register reuse use the same information.
struct SpillLiveness {
  SmallVector<BitVector> LiveIn;
  SmallVector<BitVector> Conflicts;
  SmallVector<BitVector> Clobbers;

  SpillLiveness(const MachineFunction &MF, ArrayRef<Candidate> Candidates,
                const TargetRegisterInfo &TRI) {
    unsigned Slots = MF.getFrameInfo().getObjectIndexEnd();
    BitVector IsSpill(Slots);
    for (const Candidate &C : Candidates)
      if (C.Kind == CandidateKind::Spill)
        IsSpill.set(C.FrameIndex);
    if (IsSpill.none())
      return;
    LiveIn.assign(MF.getNumBlockIDs(), BitVector(Slots));
    SmallVector<BitVector> LiveOut(LiveIn.size(), BitVector(Slots));
    Conflicts.assign(Slots, BitVector(Slots));
    Clobbers.assign(Slots, BitVector(TRI.getNumRegs()));

    auto Slot = [&](const MachineInstr &MI) -> int {
      if (MI.isDebugInstr() || MI.getNumOperands() < 3 ||
          !MI.getOperand(1).isFI())
        return -1;
      int FI = MI.getOperand(1).getIndex();
      return FI >= 0 && IsSpill.test(FI) ? FI : -1;
    };

    bool Changed;
    do {
      Changed = false;
      for (const MachineBasicBlock &MBB : llvm::reverse(MF)) {
        BitVector Live(Slots);
        for (const MachineBasicBlock *Succ : MBB.successors())
          Live |= LiveIn[Succ->getNumber()];
        LiveOut[MBB.getNumber()] = Live;
        for (const MachineInstr &MI : llvm::reverse(MBB))
          if (int FI = Slot(MI); FI >= 0) {
            if (MI.mayStore())
              Live.reset(FI);
            else
              Live.set(FI);
          }
        if (Live != LiveIn[MBB.getNumber()]) {
          LiveIn[MBB.getNumber()] = std::move(Live);
          Changed = true;
        }
      }
    } while (Changed);

    for (const MachineBasicBlock &MBB : MF) {
      BitVector Live = LiveOut[MBB.getNumber()];
      for (const MachineInstr &MI : llvm::reverse(MBB)) {
        if (MI.isCall()) {
          bool HasMask = llvm::any_of(
              MI.operands(), [](const auto &MO) { return MO.isRegMask(); });
          for (MCRegister Reg : {RISCC::S2, RISCC::S3, RISCC::S4, RISCC::S5,
                                 RISCC::S6, RISCC::S7})
            if (!HasMask || MI.modifiesRegister(Reg, &TRI))
              for (unsigned FI : Live.set_bits())
                Clobbers[FI].set(Reg);
        }
        if (int FI = Slot(MI); FI >= 0) {
          if (MI.mayStore()) {
            // Even a dead store must not overwrite another live spill.
            Conflicts[FI] |= Live;
            for (unsigned Other : Live.set_bits())
              Conflicts[Other].set(FI);
            Live.reset(FI);
          } else
            Live.set(FI);
        }
      }
      for (unsigned FI : Live.set_bits())
        Conflicts[FI] |= Live;
    }
  }
};

// A spill's reload/update/store may already use one working GPR in a loop.
// If that GPR has no other live value or clobber, keep the spill in it across
// basic blocks. An assignment from another register can become a copy when
// the working GPR is dead at that point. This promotes a private stack object,
// not a special case for the arithmetic or control-flow shape of the loop.
static bool promoteLoopSpills(MachineFunction &MF,
                              const MachineBlockFrequencyInfo &MBFI,
                              const MachineLoopInfo &MLI) {
  if (MF.getFunction().hasOptNone())
    return false;
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  auto &MFI = MF.getFrameInfo();
  unsigned Load = TII.getNativeOpcode(RISCC::LD);
  unsigned Store = TII.getNativeOpcode(RISCC::ST);
  bool Changed = false;
  for (MachineLoop *Loop : MLI.getLoopsInPreorder()) {
    if (!Loop->isInnermost())
      continue;
    auto *Entry = Loop->getLoopPreheader();
    SmallVector<MachineBasicBlock *, 4> Exits;
    Loop->getUniqueExitBlocks(Exits);
    if (!Entry || Exits.empty() || llvm::any_of(Loop->blocks(), [](auto *BB) {
          return BB->isEHPad() || BB->hasAddressTaken() ||
                 llvm::any_of(*BB, [](const MachineInstr &MI) {
                   return MI.isCall() || MI.isInlineAsm() || MI.isBundled() ||
                          MI.isCFIInstruction() ||
                          MI.hasUnmodeledSideEffects() ||
                          MI.getFlag(MachineInstr::FrameSetup) ||
                          MI.getFlag(MachineInstr::FrameDestroy);
                 });
        }))
      continue;
    SmallVector<std::pair<int, Register>, 4> Candidates;
    for (MachineBasicBlock *BB : Loop->blocks())
      for (MachineInstr &MI : *BB)
        if (MI.getOpcode() == Load && MI.getOperand(1).isFI() &&
            MFI.isSpillSlotObjectIndex(MI.getOperand(1).getIndex())) {
          std::pair<int, Register> C{MI.getOperand(1).getIndex(),
                                     MI.getOperand(0).getReg()};
          if (!llvm::is_contained(Candidates, C))
            Candidates.push_back(C);
        }
    for (auto [FI, Reg] : Candidates) {
      if (!Reg.isPhysical() || Reg == RISCC::R0 || Reg == RISCC::R7 ||
          !STI.getGPRClass()->contains(Reg) ||
          llvm::any_of(
              MF.getInStackSlotVariableDbgInfo(),
              [FI](const auto &Var) { return Var.getStackSlot() == FI; }) ||
          llvm::any_of(Loop->blocks(),
                       [Reg](auto *BB) { return BB->isLiveIn(Reg); }) ||
          llvm::any_of(Exits,
                       [Reg](auto *BB) {
                         return BB->isEHPad() || BB->hasAddressTaken() ||
                                BB->isLiveIn(Reg);
                       }) ||
          llvm::any_of(make_range(Entry->getFirstTerminator(), Entry->end()),
                       [&](const MachineInstr &MI) {
                         return MI.readsRegister(Reg, &TRI) ||
                                MI.modifiesRegister(Reg, &TRI);
                       }))
        continue;
      // Compiler spills cannot alias visible memory unless their address
      // escapes. Reject nonnative, offset, ordered and address-taking uses.
      if (!llvm::all_of(MF, [&](const MachineBasicBlock &BB) {
            return llvm::all_of(BB, [&](const MachineInstr &MI) {
              return !referencesFrameIndex(MI, FI) ||
                     (!MI.isDebugInstr() && isSimpleSpillAccess(MI, FI) &&
                      (MI.getOpcode() == Load || MI.getOpcode() == Store) &&
                      !MI.hasOrderedMemoryRef() && MI.getNumOperands() == 3);
            });
          }))
        continue;
      SmallVector<MachineInstr *, 8> Accesses;
      uint64_t Saved = 0;
      unsigned Copies = 0;
      bool Safe = true, Modified = false;
      for (MachineBasicBlock *BB : Loop->blocks()) {
        bool StoredLater = false;
        LivePhysRegs Live(TRI);
        Live.addLiveOuts(*BB);
        for (MachineInstr &MI : llvm::reverse(*BB)) {
          if (MI.isDebugInstr())
            continue;
          if (referencesFrameIndex(MI, FI)) {
            bool IsStore = MI.getOpcode() == Store;
            bool SameReg = MI.getOperand(0).getReg() == Reg;
            if (!SameReg && (!IsStore || Live.contains(Reg))) {
              Safe = false;
              break;
            }
            StoredLater = IsStore && SameReg;
            Modified |= IsStore;
            Accesses.push_back(&MI);
            if (SameReg)
              Saved =
                  SaturatingAdd(Saved, MBFI.getBlockFreq(BB).getFrequency());
            else
              ++Copies;
          } else if (MI.modifiesRegister(Reg, &TRI) && !StoredLater) {
            // Every non-reload definition must reach a store to this spill
            // before another reload or a block boundary. Otherwise it is a
            // different value which would clobber the promoted spill.
            Safe = false;
            break;
          }
          Live.stepBackward(MI);
        }
        if (!Safe)
          break;
      }
      if (!Safe || Accesses.empty())
        continue;
      SmallVector<Candidate, 1> Slot{
          {0, CandidateKind::Spill, FI, MCRegister()}};
      SpillLiveness Liveness(MF, Slot, TRI);
      bool NeedsStore = Modified && llvm::any_of(Exits, [&](auto *BB) {
                          return Liveness.LiveIn[BB->getNumber()].test(FI);
                        });
      // Dead spill values need no writeback. For live values use an existing
      // dedicated exit; never insert a store on another loop's exit path.
      if (NeedsStore &&
          (Exits.size() != 1 ||
           !llvm::all_of(Exits.front()->predecessors(),
                         [&](auto *BB) { return Loop->contains(BB); })))
        continue;
      uint64_t Added = MBFI.getBlockFreq(Entry).getFrequency();
      if (NeedsStore)
        Added = SaturatingAdd(Added,
                              MBFI.getBlockFreq(Exits.front()).getFrequency());
      // Do not grow the instruction sequence, including for -Os/-Oz. A
      // same-size move to the loop boundary is useful when it runs less often.
      if (Saved <= Added || Accesses.size() < 1 + NeedsStore + Copies)
        continue;
      TII.loadRegFromStackSlot(*Entry, Entry->getFirstTerminator(), Reg, FI,
                               STI.getGPRClass(), Register(), 0,
                               MachineInstr::NoFlags);
      if (NeedsStore) {
        auto *Exit = Exits.front();
        TII.storeRegToStackSlot(*Exit, Exit->getFirstNonPHI(), Reg, false, FI,
                                STI.getGPRClass(), Register(),
                                MachineInstr::NoFlags);
        Exit->addLiveIn(Reg);
      }
      for (MachineInstr *MI : Accesses) {
        if (MI->getOpcode() == Store && MI->getOperand(0).getReg() != Reg)
          BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
                  TII.get(TargetOpcode::COPY), Reg)
              .add(MI->getOperand(0));
        MI->eraseFromParent();
      }
      for (MachineBasicBlock *BB : Loop->blocks())
        if (!BB->isLiveIn(Reg))
          BB->addLiveIn(Reg);
      for (MachineBasicBlock &BB : MF)
        recomputeLivenessFlags(BB);
      Changed = true;
    }
  }
  return Changed;
}

static bool allocateSRegisters(MachineFunction &MF,
                               const MachineBlockFrequencyInfo &MBFI,
                               const MachineLoopInfo &MLI) {
  const RISCCSubtarget &STI = MF.getSubtarget<RISCCSubtarget>();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *Info = MF.getInfo<RISCCMachineFunctionInfo>();
  Info->clearSRegPlan();
  if (STI.isNano())
    return false;

  bool Changed = EnableLoopSpillPromotion && promoteLoopSpills(MF, MBFI, MLI);
  Changed |= EnableLoopSpillHoist && hoistLoopSpill(MF, MBFI, MLI);
  Changed |=
      EnableLoopInvariantSpills && cacheLoopInvariantSpills(MF, MBFI, MLI);

  MachineRegisterInfo &MRI = MF.getRegInfo();
  MCRegister Link = Info->getReturnAddressReg().asMCReg();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
  SmallVector<Candidate, 8> Candidates;

  for (int FI = 0; FI != MFI.getObjectIndexEnd(); ++FI) {
    if (MFI.isDeadObjectIndex(FI) || !MFI.isSpillSlotObjectIndex(FI))
      continue;
    uint64_t Weight;
    if (analyzeSpillSlot(MF, FI, MBFI, Weight))
      Candidates.push_back({Weight, CandidateKind::Spill, FI, MCRegister()});
  }

  uint64_t EntryFrequency = MBFI.getEntryFreq().getFrequency();
  bool SkipCalleeSaves = STI.getFrameLowering()->enableCalleeSaveSkip(MF);
  if (!SkipCalleeSaves)
    for (const MCPhysReg *Saved = TRI.getCalleeSavedRegs(&MF); *Saved;
         ++Saved) {
      MCRegister Reg = *Saved;
      if ((Reg == RISCC::R4 || Reg == RISCC::R5 || Reg == RISCC::R6) &&
          MRI.isPhysRegUsed(Reg))
        Candidates.push_back({SaturatingMultiply(EntryFrequency, uint64_t(2)),
                              CandidateKind::CalleeSave, -1, Reg});
    }

  // Calls define the link even when a function never returns. Only an actual
  // read (a return or forwarding tail call) needs the entry address preserved.
  bool HasLink = Link && !MRI.use_nodbg_empty(Link);
  // Freeze entry-address liveness before this pass can reuse the physical
  // link as spill storage. Later frame lowering must not mistake cache reads
  // in a non-returning function for uses of its incoming return address.
  Info->setEntryLinkIsLive(HasLink);
  bool IsLinkClobbered =
      HasLink && llvm::any_of(MF, [&](const MachineBasicBlock &MBB) {
        return llvm::any_of(MBB, [&](const MachineInstr &MI) {
          return !(MI.isCall() && MI.isReturn()) &&
                 MI.modifiesRegister(Link, &TRI);
        });
      });
  if (HasLink)
    Candidates.push_back({SaturatingMultiply(EntryFrequency, uint64_t(4)),
                          CandidateKind::ReturnAddress, -1, Link});

  llvm::stable_sort(Candidates, [](const Candidate &A, const Candidate &B) {
    if (A.Weight != B.Weight)
      return A.Weight > B.Weight;
    return A.Kind < B.Kind;
  });

  SpillLiveness Spills(MF, Candidates, TRI);
  SmallVector<MCRegister, 6> Free;
  // Prefer every caller-saved cache register before paying the entry/exit
  // save cost of S5/S6. S7 can be free when the entry link is private or dead.
  for (MCRegister Reg :
       {RISCC::S2, RISCC::S3, RISCC::S4, RISCC::S7, RISCC::S5, RISCC::S6})
    // Explicit operands still reserve a register, including link moves before
    // tail calls. Register-mask clobbers are checked for each candidate below.
    if (!MRI.isPhysRegUsed(Reg, /*SkipRegMaskTest=*/true) ||
        (Reg == Link && !IsLinkClobbered))
      Free.push_back(Reg);

  // Flexible candidates avoid the precolored link register until its value is
  // less valuable than all the other available cache entries.
  if (HasLink && !IsLinkClobbered)
    llvm::stable_sort(Free, [Link](MCRegister A, MCRegister B) {
      return (A == Link) < (B == Link);
    });

  bool KeptLink = !HasLink;
  SmallVector<std::pair<int, MCRegister>, 6> SpillAssignments;
  auto UsedBySpill = [&](MCRegister Reg) {
    return llvm::any_of(SpillAssignments,
                        [Reg](const auto &A) { return A.second == Reg; });
  };
  for (const Candidate &C : Candidates) {
    if (C.Kind == CandidateKind::ReturnAddress && !IsLinkClobbered) {
      if (!UsedBySpill(Link) && takeRegister(Free, Link))
        KeptLink = true;
      continue;
    }
    if (C.Kind == CandidateKind::Spill) {
      auto It = llvm::find_if(Free, [&](MCRegister Reg) {
        // First use of a callee-saved cache register, or eviction of an
        // otherwise untouched link, adds MFS/ST at entry and LD/MTS at exit.
        // Moving the spill itself saves no code bytes. Charge that setup
        // once; disjoint spills can reuse a paid register.
        bool NeedsSave =
            (!SkipCalleeSaves && (Reg == RISCC::S5 || Reg == RISCC::S6)) ||
            (HasLink && !IsLinkClobbered && Reg == Link);
        if (NeedsSave && !UsedBySpill(Reg) &&
            (MF.getFunction().hasOptSize() ||
             C.Weight <= SaturatingMultiply(EntryFrequency, uint64_t(4))))
          return false;
        return !Spills.Clobbers[C.FrameIndex].test(Reg) &&
               llvm::none_of(SpillAssignments, [&](const auto &A) {
                 return A.second == Reg &&
                        Spills.Conflicts[C.FrameIndex].test(A.first);
               });
      });
      if (It != Free.end())
        SpillAssignments.emplace_back(C.FrameIndex, *It);
      continue;
    }

    auto SRegIt = llvm::find_if(Free, [&](MCRegister Reg) {
      return (Reg == RISCC::S2 || Reg == RISCC::S3 || Reg == RISCC::S4 ||
              Reg == RISCC::S7) &&
             !UsedBySpill(Reg) && isPreservedByAllCalls(MF, Reg, TRI);
    });
    if (SRegIt == Free.end())
      continue;
    if (C.Kind == CandidateKind::ReturnAddress) {
      Info->setLRSaveReg(*SRegIt);
      KeptLink = true;
    } else
      Info->setCalleeSavedSReg(C.Reg, *SRegIt);
    Free.erase(SRegIt);
    Changed = true;
  }

  if (!KeptLink && Info->getLRSpillFI() < 0) {
    Info->setLRSpillFI(MFI.CreateStackObject(STI.getSlotSize(),
                                             STI.getStackAlignment(), false));
    Changed = true;
  }
  for (auto [FI, SReg] : SpillAssignments) {
    moveSpillToSReg(MF, FI, SReg, *STI.getInstrInfo());
    for (MachineBasicBlock &MBB : MF)
      if (Spills.LiveIn[MBB.getNumber()].test(FI) && !MBB.isLiveIn(SReg))
        MBB.addLiveIn(SReg);
  }
  return Changed || !SpillAssignments.empty();
}

class RISCCSRegAllocatorLegacy final : public MachineFunctionPass {
public:
  static char ID;
  RISCCSRegAllocatorLegacy() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "RISC-C S-register allocator";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  bool runOnMachineFunction(MachineFunction &MF) override {
    return allocateSRegisters(
        MF, getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI(),
        getAnalysis<MachineLoopInfoWrapperPass>().getLI());
  }
};

} // namespace

char RISCCSRegAllocatorLegacy::ID = 0;

INITIALIZE_PASS_BEGIN(RISCCSRegAllocatorLegacy, DEBUG_TYPE,
                      "Plan use of the RISC-C S-register cache", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(RISCCSRegAllocatorLegacy, DEBUG_TYPE,
                    "Plan use of the RISC-C S-register cache", false, false)

FunctionPass *llvm::createRISCCSRegAllocatorLegacyPass() {
  return new RISCCSRegAllocatorLegacy();
}

PreservedAnalyses
RISCCSRegAllocatorPass::run(MachineFunction &MF,
                            MachineFunctionAnalysisManager &MFAM) {
  bool Changed =
      allocateSRegisters(MF, MFAM.getResult<MachineBlockFrequencyAnalysis>(MF),
                         MFAM.getResult<MachineLoopAnalysis>(MF));
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<MachineBlockFrequencyAnalysis>();
  PA.preserve<MachineLoopAnalysis>();
  return PA;
}
