//===-- RISCCConstantIslandPass.cpp - Place literal pools
//------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCConstantIslandPass.h"
#include "RISCC.h"
#include "RISCCInstrInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/BranchRelaxation.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "riscc-constant-islands"

namespace {
struct LiteralUse {
  MachineInstr *MI;
  unsigned ValueIndex;
  unsigned Offset;
  bool Repaired = false;
};

struct LiteralEntry {
  unsigned ValueIndex;
  unsigned FirstUse, LastUse;
  SmallVector<MachineInstr *, 2> Users;
};

struct LiteralPool {
  unsigned Offset = 0;
  unsigned LeadingBytes = 0;
  bool NeedsJump = false;
  SmallVector<LiteralEntry, 8> Entries;

  bool empty() const { return Entries.empty(); }
};

struct PoolSite {
  MachineInstr *Before;
  unsigned Offset;
  unsigned LoopDepth;
  bool AtBlockStart;
};

class RISCCConstantIslands {
  MachineFunction *MF;
  const RISCCInstrInfo &TII;
  SmallVector<MachineOperand, 8> Values;
  SmallVector<LiteralUse, 32> AllUses;
  SmallVector<LiteralPool, 8> LiteralPools;
  DenseMap<const MachineBasicBlock *, unsigned> PoolAfter;
  DenseMap<const MachineBasicBlock *, unsigned> PoolBeforeBlock;
  DenseMap<const MachineInstr *, unsigned> PoolBeforeInstruction;
  DenseMap<const MachineInstr *, unsigned> Offsets;
  bool ExactLayout = false;
  DenseMap<const MCSymbol *, MachineInstr *> Symbols;
  SmallVector<std::pair<MachineInstr *, unsigned>, 16> Entries;

  static bool isIsland(const MachineBasicBlock &MBB) {
    return !MBB.empty() && MBB.front().getOpcode() == RISCC::CONSTPOOL_ENTRY;
  }

  static int literalOperand(const MachineInstr &MI) {
    switch (MI.getOpcode()) {
    case RISCC::LDPC:
    case RISCC::LDPC_BRANCH:
      return 1;
    case RISCC::CALL32_LITERAL:
    case RISCC::TAIL32_LITERAL:
      return 0;
    default:
      return -1;
    }
  }

  unsigned valueIndex(const MachineOperand &Value) {
    for (auto [I, V] : enumerate(Values))
      if (V.isIdenticalTo(Value))
        return I;
    Values.push_back(Value);
    return Values.size() - 1;
  }

  void assign(MachineInstr &Use, MachineInstr &Entry) {
    MachineOperand &Op = Use.getOperand(literalOperand(Use));
    Op.ChangeToMCSymbol(Entry.getOperand(0).getMCSymbol());
    Op.setOffset(0);
    if (Use.getOpcode() == RISCC::LDPC_BRANCH)
      Use.setDesc(TII.get(RISCC::LDPC));
  }

  MachineInstr &addEntry(MachineBasicBlock &Island, unsigned Value) {
    MF->ensureAlignment(Align(4));
    MCSymbol *Label = MF->getContext().createTempSymbol();
    MachineInstr &MI = *BuildMI(Island, Island.end(), DebugLoc(),
                                TII.get(RISCC::CONSTPOOL_ENTRY))
                            .addSym(Label)
                            .add(Values[Value]);
    Symbols[Label] = &MI;
    Entries.emplace_back(&MI, Value);
    return MI;
  }

  MachineBasicBlock &insertIsland(MachineFunction::iterator Where) {
    auto *Island = MF->CreateMachineBasicBlock();
    if (Where != MF->begin())
      Island->setSectionID(std::prev(Where)->getSectionID());
    Island->setAlignment(Align(4));
    if (Where != MF->begin()) {
      Island->setIsEndSection(std::prev(Where)->isEndSection());
      std::prev(Where)->setIsEndSection(false);
    }
    MF->insert(Where, Island);
    return *Island;
  }

  MachineBasicBlock &splitBefore(MachineInstr &Before) {
    MachineBasicBlock &Head = *Before.getParent();
    auto *Tail = MF->CreateMachineBasicBlock(Head.getBasicBlock());
    Tail->setSectionID(Head.getSectionID());
    Tail->setIsEndSection(Head.isEndSection());
    Head.setIsEndSection(false);
    MF->insert(std::next(Head.getIterator()), Tail);
    Tail->splice(Tail->end(), &Head, Before.getIterator(), Head.end());
    Tail->transferSuccessorsAndUpdatePHIs(&Head);
    Head.addSuccessor(Tail);
    if (MF->getProperties().hasTracksLiveness())
      recomputeLiveIns(*Tail);
    return *Tail;
  }

  MachineBasicBlock &islandBefore(MachineBasicBlock &Next) {
    assert(&Next != &MF->front() && "island before function entry");
    MachineBasicBlock &Prev = *Next.getPrevNode();
    if (!isIsland(Prev) && Prev.canFallThrough())
      BuildMI(Prev, Prev.end(), DebugLoc(), TII.get(RISCC::JMP8)).addMBB(&Next);
    return insertIsland(Next.getIterator());
  }

  void materialize(unsigned Index, MachineBasicBlock &Island) {
    for (const LiteralEntry &E : LiteralPools[Index].Entries) {
      MachineInstr &Entry = addEntry(Island, E.ValueIndex);
      for (MachineInstr *Use : E.Users)
        assign(*Use, Entry);
    }
  }

  void materializePools() {
    // Materialize natural gaps first: splitting a block afterwards keeps its
    // trailing island after the new tail block.
    SmallVector<MachineBasicBlock *, 16> Blocks;
    SmallVector<MachineInstr *, 16> Instructions;
    for (MachineBasicBlock &MBB : *MF) {
      Blocks.push_back(&MBB);
      for (MachineInstr &MI : MBB)
        if (PoolBeforeInstruction.contains(&MI))
          Instructions.push_back(&MI);
    }
    for (MachineBasicBlock *MBB : Blocks)
      if (auto It = PoolAfter.find(MBB);
          It != PoolAfter.end() && !LiteralPools[It->second].empty())
        materialize(It->second, insertIsland(std::next(MBB->getIterator())));
    for (MachineBasicBlock *MBB : Blocks)
      if (auto It = PoolBeforeBlock.find(MBB);
          It != PoolBeforeBlock.end() && !LiteralPools[It->second].empty())
        materialize(It->second, islandBefore(*MBB));
    for (MachineInstr *MI : Instructions)
      if (unsigned Index = PoolBeforeInstruction.lookup(MI);
          !LiteralPools[Index].empty()) {
        auto &Next = splitBefore(*MI);
        materialize(Index, islandBefore(Next));
      }
  }

  // A pool first receives uses before its gap, then uses after it. Keep
  // early forward uses near the start and late backward uses near the end.
  static std::optional<LiteralPool> layoutPool(LiteralPool Pool,
                                               ArrayRef<LiteralUse> Uses) {
    SmallVector<LiteralEntry, 8> Entries;
    for (const LiteralUse &Use : Uses) {
      auto It = llvm::find_if(Entries, [&](const LiteralEntry &Entry) {
        return Entry.ValueIndex == Use.ValueIndex;
      });
      if (It == Entries.end())
        Entries.push_back({Use.ValueIndex, Use.Offset, Use.Offset, {Use.MI}});
      else {
        It->LastUse = Use.Offset;
        It->Users.push_back(Use.MI);
      }
    }
    bool Backward = !Uses.empty() && Uses.front().Offset >= Pool.Offset;
    llvm::sort(Entries, [=](const LiteralEntry &A, const LiteralEntry &B) {
      return Backward ? A.LastUse < B.LastUse : A.FirstUse < B.FirstUse;
    });
    // Start with shared copies. A backward use that cannot reach its shared
    // word needs a later copy; account for that growth before accepting others.
    unsigned Words = Pool.Entries.size() + Entries.size();
    SmallVector<unsigned, 8> Shared;
    for (const LiteralEntry &Entry : Entries) {
      auto Earlier = llvm::find_if(Pool.Entries, [&](const LiteralEntry &E) {
        return E.ValueIndex == Entry.ValueIndex;
      });
      Shared.push_back(Earlier - Pool.Entries.begin());
      if (Earlier != Pool.Entries.end())
        --Words;
    }
    bool Changed;
    do {
      Changed = false;
      for (auto [I, Entry] : enumerate(Entries)) {
        unsigned Index = Shared[I];
        if (Index == Pool.Entries.size())
          continue;
        int64_t Distance =
            int64_t(Entry.LastUse) - Pool.Offset + 2 + 4 * (Words - Index);
        if (Distance > 256) {
          Shared[I] = Pool.Entries.size();
          ++Words;
          Changed = true;
        }
      }
    } while (Changed);
    unsigned Unshared = Pool.Entries.size();
    for (auto [I, Entry] : enumerate(Entries)) {
      if (Shared[I] != Unshared) {
        LiteralEntry &Earlier = Pool.Entries[Shared[I]];
        Earlier.LastUse = Entry.LastUse;
        Earlier.Users.append(Entry.Users);
      } else
        Pool.Entries.push_back(std::move(Entry));
    }

    // A jump over at most 63 words fits even with two alignment bytes.
    if (Pool.NeedsJump && Pool.Entries.size() > 63)
      return std::nullopt;
    for (auto [I, Entry] : enumerate(Pool.Entries)) {
      // Up to two alignment bytes cancel the forward load's PC+2 bias.
      int64_t Forward =
          int64_t(Pool.Offset) + Pool.LeadingBytes + 4 * I - Entry.FirstUse;
      int64_t Backward = int64_t(Entry.LastUse) - Pool.Offset + 2 +
                         4 * (Pool.Entries.size() - I);
      if ((Entry.FirstUse < Pool.Offset && Forward > 254) ||
          (Entry.LastUse >= Pool.Offset && Backward > 256))
        return std::nullopt;
    }
    return Pool;
  }

  void planLiteralPools() {
    LiteralPools.clear();
    PoolAfter.clear();
    PoolBeforeBlock.clear();
    PoolBeforeInstruction.clear();
    LiteralPools.push_back({});

    MachineDominatorTree MDT(*MF);
    MachineLoopInfo Loops(MDT);
    unsigned LeftPool = 0;
    SmallVector<LiteralUse, 8> Uses;
    SmallVector<PoolSite, 32> Sites;
    unsigned Offset = 0;
    const auto &TII = *MF->getSubtarget().getInstrInfo();

    // Plan each region between natural gaps, inserting pools only where
    // neither end is reachable.
    for (MachineBasicBlock &MBB : *MF) {
      if (MBB.isBeginSection())
        LeftPool = 0;
      unsigned BlockOffset = Offset;
      Align Alignment = MBB.getAlignment();
      // Instructions are two-byte aligned. Account for the largest padding
      // between a pool and the next block's label.
      Offset += Alignment.value() > 2 ? Alignment.value() - 2 : 0;
      bool AtBlockStart = true;
      bool AfterBranchLoad = false;
      for (MachineInstr &MI : MBB) {
        unsigned Size = TII.getInstSizeInBytes(MI);
        if (Size && !AfterBranchLoad) {
          unsigned Depth = Loops.getLoopDepth(&MBB);
          // A pool before the block label is skipped by branches to that
          // block, including loop backedges. Only layout fallthrough needs
          // the jump, so charge its loop depth to the preceding block.
          if (AtBlockStart && &MBB != &MF->front() &&
              MBB.getPrevNode()->getSectionID() == MBB.getSectionID())
            Sites.push_back({&MI, BlockOffset,
                             Loops.getLoopDepth(MBB.getPrevNode()), true});
          if (!MI.isBundle())
            Sites.push_back({&MI, Offset, Depth, false});
        }
        if (int Op = literalOperand(MI); Op >= 0) {
          LiteralUse Use{&MI, valueIndex(MI.getOperand(Op)), Offset};
          Uses.push_back(Use);
          AllUses.push_back(Use);
        }
        if (Size) {
          AtBlockStart = false;
          AfterBranchLoad = MI.getOpcode() == RISCC::LDPC_BRANCH;
        }
        Offset += Size;
      }

      auto Last = MBB.getLastNonDebugInstr();
      if (Last == MBB.end() || !Last->isBarrier())
        continue;

      unsigned RightPool = LiteralPools.size();
      LiteralPools.push_back({Offset});
      planRegion(LeftPool, RightPool, Uses, Sites);
      PoolAfter[&MBB] = RightPool;
      LeftPool = RightPool;
      Uses.clear();
      Sites.clear();
    }

    if (!Uses.empty())
      report_fatal_error("RISC-C function has no terminal literal-pool gap");
  }

  void planRegion(unsigned Left, unsigned Right, ArrayRef<LiteralUse> Uses,
                  ArrayRef<PoolSite> Sites) {
    while (true) {
      unsigned BackCount =
          llvm::upper_bound(Uses, LiteralPools[Left].Offset + 254,
                            [](unsigned Offset, const LiteralUse &Use) {
                              return Offset < Use.Offset;
                            }) -
          Uses.begin();
      if (Left == 0)
        BackCount = 0;
      auto Before = layoutPool(LiteralPools[Left], Uses.take_front(BackCount));
      while (!Before)
        Before = layoutPool(LiteralPools[Left], Uses.take_front(--BackCount));
      if (auto After =
              layoutPool(LiteralPools[Right], Uses.drop_front(BackCount))) {
        LiteralPools[Left] = std::move(*Before);
        LiteralPools[Right] = std::move(*After);
        return;
      }

      // Preserve natural barrier gaps wherever they suffice. Otherwise find
      // a shared island, preferring shallower loops, then block boundaries,
      // then the farthest reachable site to cover more uses with one jump.
      const PoolSite *Best = nullptr;
      unsigned BestCount = 0, BestSplit = 0;
      for (const PoolSite &Site : Sites) {
        if (Site.Offset <= LiteralPools[Left].Offset)
          continue;
        if (Site.Offset > Uses[BackCount].Offset + 252)
          break;
        unsigned Count =
            llvm::lower_bound(Uses, Site.Offset,
                              [](const LiteralUse &Use, unsigned Offset) {
                                return Use.Offset < Offset;
                              }) -
            Uses.begin();
        LiteralPool Candidate{Site.Offset, 2, true};
        unsigned Split = std::min(BackCount, Count);
        ArrayRef<LiteralUse> Forward = Uses.slice(Split, Count - Split);
        if (!layoutPool(Candidate, Forward) ||
            (!Count && !layoutPool(Candidate, Uses.take_front(1))))
          continue;
        if (!Best || Site.LoopDepth < Best->LoopDepth ||
            (Site.LoopDepth == Best->LoopDepth &&
             (Site.AtBlockStart > Best->AtBlockStart ||
              (Site.AtBlockStart == Best->AtBlockStart &&
               Site.Offset > Best->Offset)))) {
          Best = &Site;
          BestCount = Count;
          BestSplit = Split;
        }
      }
      assert(Best && "no literal island site within LDPC range");
      unsigned Island = LiteralPools.size();
      LiteralPools.push_back({Best->Offset, 2, true});
      if (Best->AtBlockStart)
        PoolBeforeBlock[Best->Before->getParent()] = Island;
      else
        PoolBeforeInstruction[Best->Before] = Island;
      LiteralPools[Left] =
          *layoutPool(LiteralPools[Left], Uses.take_front(BestSplit));
      LiteralPools[Island] = *layoutPool(
          LiteralPools[Island], Uses.slice(BestSplit, BestCount - BestSplit));
      Left = Island;
      Uses = Uses.drop_front(BestCount);
      Sites = Sites.drop_front(Best - Sites.begin() + 1);
    }
  }

  unsigned alignBlock(unsigned Offset, const MachineBasicBlock &MBB) const {
    unsigned Padding = 0;
    if (ExactLayout)
      Padding = offsetToAlignment(Offset, MBB.getAlignment());
    else if (MBB.getAlignment().value() > 2)
      Padding = MBB.getAlignment().value() - 2;
    if (unsigned Max = MBB.getMaxBytesForAlignment()) {
      if (ExactLayout && Padding > Max)
        Padding = 0;
      else
        Padding = std::min(Padding, Max & ~1u);
    }
    return Offset + Padding;
  }

  void computeOffsets() {
    // With known instruction sizes and sufficient function alignment these
    // are exact offsets. Otherwise every interval includes maximum padding,
    // so subtracting offsets conservatively bounds its instruction span.
    Align SectionAlignment = MF->getAlignment();
    ExactLayout = llvm::all_of(*MF, [&](const MachineBasicBlock &MBB) {
      if (MBB.isBeginSection() && &MBB != &MF->front())
        SectionAlignment = MBB.getAlignment();
      return MBB.getAlignment() <= SectionAlignment &&
             llvm::none_of(MBB, [](const MachineInstr &MI) {
               return MI.isInlineAsm() || MI.isBundle();
             });
    });
    unsigned Offset = 0;
    Offsets.clear();
    for (MachineBasicBlock &MBB : *MF) {
      if (MBB.isBeginSection())
        Offset = 0;
      Offset = alignBlock(Offset, MBB);
      for (MachineInstr &MI : MBB) {
        Offsets[&MI] = Offset;
        Offset += TII.getInstSizeInBytes(MI);
      }
    }
  }

  bool inRange(const MachineInstr &Use, const MachineInstr &Entry) const {
    if (Use.getParent()->getSectionID() != Entry.getParent()->getSectionID())
      return false;
    int64_t Distance =
        int64_t(Offsets.lookup(&Entry)) - Offsets.lookup(&Use) - 2;
    return !(Distance & 1) && isInt<8>(Distance / 2);
  }

  // Predict insertion at a block boundary, including the change in every
  // intervening block's alignment. Instruction splitting is only the fallback.
  bool gapInRange(const MachineInstr &Use, const MachineBasicBlock &Next,
                  bool NeedsJump) const {
    if (Next.getPrevNode()->getSectionID() != Use.getParent()->getSectionID())
      return false;
    unsigned Offset = 0, UseOffset = 0, EntryOffset = 0;
    for (const MachineBasicBlock &MBB : *MF) {
      if (&MBB == &Next) {
        unsigned Start = Offset + (NeedsJump ? 2 : 0);
        EntryOffset = ExactLayout ? alignTo(Start, 4) : Start + 2;
        Offset = EntryOffset + 4;
      }
      if (MBB.isBeginSection())
        Offset = 0;
      Offset = alignBlock(Offset, MBB);
      for (const MachineInstr &MI : MBB) {
        if (&MI == &Use)
          UseOffset = Offset;
        Offset += TII.getInstSizeInBytes(MI);
      }
    }
    int64_t Distance = int64_t(EntryOffset) - UseOffset - 2;
    return !(Distance & 1) && isInt<8>(Distance / 2);
  }

  void repairUse(LiteralUse &Use) {
    // If later growth invalidated an earlier repair, finish with a local
    // island. This bounds retries even when pruning reopens a previous gap.
    if (Use.Repaired) {
      placeAdjacent(Use);
      return;
    }
    Use.Repaired = true;
    for (auto [Entry, Value] : Entries)
      if (Value == Use.ValueIndex && inRange(*Use.MI, *Entry)) {
        assign(*Use.MI, *Entry);
        return;
      }

    MachineDominatorTree MDT(*MF);
    MachineLoopInfo Loops(MDT);
    MachineBasicBlock *Best = nullptr;
    bool BestJump = true;
    unsigned BestDepth = ~0u;
    for (MachineBasicBlock &Next : *MF) {
      if (&Next == &MF->front() || isIsland(Next) ||
          isIsland(*Next.getPrevNode()))
        continue;
      MachineBasicBlock &Prev = *Next.getPrevNode();
      bool NeedsJump = Prev.canFallThrough();
      unsigned Depth = Loops.getLoopDepth(&Prev);
      if ((!Best || NeedsJump < BestJump ||
           (NeedsJump == BestJump && Depth < BestDepth)) &&
          gapInRange(*Use.MI, Next, NeedsJump)) {
        Best = &Next;
        BestJump = NeedsJump;
        BestDepth = Depth;
      }
    }
    if (Best) {
      assign(*Use.MI, addEntry(islandBefore(*Best), Use.ValueIndex));
      return;
    }

    placeAdjacent(Use);
  }

  void placeAdjacent(LiteralUse &Use) {
    // An adjacent island always fits. Keep branch address loads with their
    // JALR so no extra jump executes between the load and the transfer.
    MachineBasicBlock &MBB = *Use.MI->getParent();
    if (Values[Use.ValueIndex].isMBB() || Use.MI->isBarrier()) {
      auto &Island = insertIsland(std::next(MBB.getIterator()));
      assign(*Use.MI, addEntry(Island, Use.ValueIndex));
      return;
    }
    auto Next = std::next(Use.MI->getIterator());
    if (Next != MBB.end()) {
      auto &Tail = splitBefore(*Next);
      assign(*Use.MI, addEntry(islandBefore(Tail), Use.ValueIndex));
    } else {
      assert(MBB.getNextNode() && "literal use at unterminated function end");
      assign(*Use.MI,
             addEntry(islandBefore(*MBB.getNextNode()), Use.ValueIndex));
    }
  }

  bool removeUnusedEntries() {
    SmallPtrSet<const MCSymbol *, 32> Used;
    for (const LiteralUse &Use : AllUses)
      Used.insert(Use.MI->getOperand(literalOperand(*Use.MI)).getMCSymbol());
    unsigned OldSize = Entries.size();
    llvm::erase_if(Entries, [&](auto E) {
      MachineInstr *Entry = E.first;
      MCSymbol *Label = Entry->getOperand(0).getMCSymbol();
      if (Used.contains(Label))
        return false;
      Symbols.erase(Label);
      MachineBasicBlock &Island = *Entry->getParent();
      Entry->eraseFromParent();
      if (Island.empty()) {
        MachineBasicBlock *Prev = Island.getPrevNode();
        MachineBasicBlock *Next = Island.getNextNode();
        Prev->setIsEndSection(Island.isEndSection());
        Island.eraseFromParent();
        // An empty island no longer needs its fallthrough skip.
        auto Last = Prev->getLastNonDebugInstr();
        if (!Prev->isEndSection() && Last != Prev->end() &&
            Last->getOpcode() == RISCC::JMP8 &&
            Last->getOperand(0).getMBB() == Next)
          Last->eraseFromParent();
      }
      return true;
    });
    return Entries.size() != OldSize;
  }

public:
  explicit RISCCConstantIslands(MachineFunction &MF)
      : MF(&MF), TII(*MF.getSubtarget<RISCCSubtarget>().getInstrInfo()) {}

  bool run() {
    if (!MF->getSubtarget<RISCCSubtarget>().isRC32())
      return false;
    planLiteralPools();
    if (!AllUses.empty())
      MF->ensureAlignment(Align(4));
    materializePools();
    bool Changed = !AllUses.empty();
    // Repair the existing pools after branch expansion. Replanning around
    // the new branch blocks would duplicate words already shared across a gap.
    while (true) {
      bool BranchChange = relaxBranches(*MF);
      Changed |= BranchChange;
      for (MachineBasicBlock &MBB : *MF)
        for (MachineInstr &MI : MBB)
          if (MI.getOpcode() == RISCC::LDPC_BRANCH)
            AllUses.push_back({&MI, valueIndex(MI.getOperand(1)), 0});
      computeOffsets();
      bool Repaired = false;
      for (LiteralUse &Use : AllUses) {
        const MachineOperand &Op = Use.MI->getOperand(literalOperand(*Use.MI));
        if (Op.isMCSymbol() &&
            inRange(*Use.MI, *Symbols.lookup(Op.getMCSymbol())))
          continue;
        repairUse(Use);
        computeOffsets();
        Repaired = true;
      }
      if (!Repaired && !removeUnusedEntries())
        break;
      Changed = true;
    }
    MF->RenumberBlocks();
    return Changed;
  }
};

class RISCCConstantIslandLegacy final : public MachineFunctionPass {
public:
  static char ID;
  RISCCConstantIslandLegacy() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "RISC-C constant islands"; }
  bool runOnMachineFunction(MachineFunction &MF) override {
    return RISCCConstantIslands(MF).run();
  }
};
} // namespace

char RISCCConstantIslandLegacy::ID = 0;
INITIALIZE_PASS(RISCCConstantIslandLegacy, DEBUG_TYPE,
                "RISC-C constant islands", false, false)

FunctionPass *llvm::createRISCCConstantIslandPass() {
  return new RISCCConstantIslandLegacy();
}

PreservedAnalyses
RISCCConstantIslandPass::run(MachineFunction &MF,
                             MachineFunctionAnalysisManager &) {
  if (RISCCConstantIslands(MF).run())
    return getMachineFunctionPassPreservedAnalyses();
  return PreservedAnalyses::all();
}
