//===-- RISCCConstantIslandPass.cpp - Place literal pools ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCConstantIslandPass.h"
#include "RISCC.h"
#include "RISCCConstantPoolValue.h"
#include "RISCCInstrInfo.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/BranchRelaxation.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "riscc-constant-islands"

namespace {
static cl::opt<bool> EnableSharedLiterals(
    "riscc-share-literals", cl::Hidden, cl::init(true),
    cl::desc("Merge reachable duplicate RISC-C literal words for size"));
static cl::opt<bool> EnablePackedLiterals(
    "riscc-pack-literals", cl::Hidden, cl::init(true),
    cl::desc(
        "Pack RISC-C literal pools for size, including padding and skips"));
static cl::opt<bool> EnableEvacuatedLiterals(
    "riscc-evacuate-literals", cl::Hidden, cl::init(true),
    cl::desc("Try evacuating literal pools into two reachable gaps"));
static cl::opt<bool> EnableLiteralMotion(
    "riscc-pack-literal-motion", cl::Hidden, cl::init(true),
    cl::desc("Try local scheduling to remove RISC-C literal-pool skips"));
static cl::opt<bool> EnableLiteralHoisting(
    "riscc-pack-literal-hoisting", cl::Hidden, cl::init(true),
    cl::desc("Try earlier loads when packing RISC-C literal pools"));

// Late copy and return cleanup can leave a block containing just a jump.
// Thread explicit predecessors only when deleting it preserves every compact
// branch's reach. Do this before placing pools, whose load ranges are separate.
static bool removeForwardingBlocks(MachineFunction &MF) {
  if (MF.getFunction().hasOptNone())
    return false;
  const auto &TII = *MF.getSubtarget<RISCCSubtarget>().getInstrInfo();
  for (const MachineBasicBlock &BB : MF)
    if (BB.getAlignment() > MF.getAlignment() ||
        llvm::any_of(BB,
                     [](const MachineInstr &MI) { return MI.isInlineAsm(); }))
      return false;

  bool Changed = false;
  for (MachineBasicBlock &BB : make_early_inc_range(MF)) {
    if (&BB == &MF.front() || BB.pred_empty() || BB.size() != 1 ||
        BB.front().getOpcode() != RISCC::JMP8 || BB.succ_size() != 1 ||
        BB.hasAddressTaken() || BB.isEHPad() || BB.isBeginSection() ||
        BB.isEndSection())
      continue;
    auto *Dest = TII.getBranchDestBlock(BB.front());
    if (!Dest || Dest == &BB || Dest->isEHPad() || !BB.sameSection(Dest))
      continue;
    // Do not change the meaning of implicit layout fallthrough, indirect
    // branches, EH edges, or block references that are not ordinary branches.
    if (llvm::any_of(BB.predecessors(), [&](MachineBasicBlock *Pred) {
          MachineBasicBlock *TBB, *FBB;
          SmallVector<MachineOperand, 4> Cond;
          return !BB.sameSection(Pred) ||
                 (Pred->getNextNode() == &BB && Pred->canFallThrough()) ||
                 TII.analyzeBranch(*Pred, TBB, FBB, Cond, false) ||
                 (TBB != &BB && FBB != &BB);
        }))
      continue;

    DenseMap<const MachineBasicBlock *, uint64_t> BlockOffsets;
    DenseMap<const MachineInstr *, uint64_t> InstOffsets;
    uint64_t Offset = 0;
    for (const MachineBasicBlock &Other : MF) {
      if (&Other == &BB)
        continue;
      Offset = alignTo(Offset, Other.getAlignment());
      BlockOffsets[&Other] = Offset;
      for (const MachineInstr &MI : Other) {
        InstOffsets[&MI] = Offset;
        Offset += TII.getInstSizeInBytes(MI);
      }
    }
    bool InRange = true;
    for (auto [MI, PC] : InstOffsets)
      if (auto *Target = TII.getBranchDestBlock(*MI)) {
        if (Target == &BB)
          Target = Dest;
        InRange &= TII.isBranchOffsetInRange(
            MI->getOpcode(),
            int64_t(BlockOffsets.lookup(Target)) - int64_t(PC));
      }
    if (!InRange)
      continue;

    while (!BB.pred_empty())
      (*BB.pred_begin())->ReplaceUsesOfBlockWith(&BB, Dest);
    BB.removeSuccessor(Dest);
    BB.eraseFromParent();
    Changed = true;
  }
  if (Changed)
    MF.RenumberBlocks();
  return Changed;
}

// When two adjacent exits are equally likely, branch over the shorter one.
// This reduces the branch span without duplicating instructions or preferring
// either outcome when profile information distinguishes them.
static bool shortenExitBranches(MachineFunction &MF) {
  if (MF.getFunction().hasOptNone())
    return false;
  const auto &TII = *MF.getSubtarget<RISCCSubtarget>().getInstrInfo();
  MachineBranchProbabilityInfo MBPI;
  bool Changed = false;
  for (MachineBasicBlock &Head : MF) {
    auto Branch = Head.getFirstTerminator();
    if (Head.succ_size() != 2 || Branch == Head.end() ||
        !Branch->isConditionalBranch() ||
        std::next(Branch) != Head.end())
      continue;
    auto *Fall = Head.getNextNode();
    auto *Taken = TII.getBranchDestBlock(*Branch);
    if (!Fall || !Taken || Fall->getNextNode() != Taken ||
        !Head.isSuccessor(Fall) || !Fall->isReturnBlock() ||
        !Taken->isReturnBlock() || !Fall->succ_empty() || !Taken->succ_empty() ||
        Fall->pred_size() != 1 || Taken->pred_size() != 1 ||
        Fall->hasAddressTaken() || Taken->hasAddressTaken() ||
        Fall->isEHPad() || Taken->isEHPad() ||
        !Head.sameSection(Fall) || !Head.sameSection(Taken) ||
        Fall->isBeginSection() || Taken->isBeginSection() || Fall->isEndSection() ||
        Fall->getAlignment() > Align(2) || Taken->getAlignment() > Align(2) ||
        MBPI.getEdgeProbability(&Head, Fall) !=
            MBPI.getEdgeProbability(&Head, Taken))
      continue;
    auto Size = [&](const MachineBasicBlock &BB) {
      unsigned Bytes = 0;
      for (const auto &MI : BB) {
        if (MI.isCFIInstruction() || MI.isInlineAsm())
          return UINT_MAX;
        Bytes += TII.getInstSizeInBytes(MI);
      }
      return Bytes;
    };
    unsigned FallSize = Size(*Fall), TakenSize = Size(*Taken);
    if (FallSize == UINT_MAX || TakenSize >= FallSize)
      continue;
    Branch->setDesc(TII.get(TII.getOppositeBranchOpcode(Branch->getOpcode())));
    Branch->getOperand(0).setMBB(Fall);
    Fall->setIsEndSection(Taken->isEndSection());
    Taken->setIsEndSection(false);
    MF.splice(Fall->getIterator(), Taken);
    Changed = true;
  }
  return Changed;
}

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

// A prospective layout, not a mutation of the CFG. Each placement is in a
// source island or a new island immediately after After. Keeping the original
// instructions until the whole layout is checked also preserves literal labels.
struct PoolPacking {
  struct Placement {
    MachineBasicBlock *Destination = nullptr;
    MachineBasicBlock *After = nullptr;
    SmallVector<MachineInstr *, 16> Words;
  };
  SmallVector<MachineBasicBlock *, 2> Sources;
  SmallVector<Placement, 2> Placements{Placement{}};
  DenseMap<MachineInstr *, MachineInstr *> Redirects;
  SmallPtrSet<MachineInstr *, 16> Relocated;
  SmallPtrSet<MachineInstr *, 2> RemovedJumps;
  unsigned Bytes = 0;
  unsigned LoopDepth = 0;

  PoolPacking(MachineBasicBlock *First, MachineBasicBlock *Second)
      : Sources{First} {
    if (Second)
      Sources.push_back(Second);
  }

  bool removes(const MachineBasicBlock &Island) const {
    return llvm::none_of(
               Placements,
               [&](const Placement &P) { return &Island == P.Destination; }) &&
           llvm::all_of(Island, [&](const MachineInstr &MI) {
             return Relocated.contains(&MI);
           });
  }
};

// Between accepted packings, code blocks do not change except for removal of
// a final island-skip branch. Cache their sizes and the offsets inspected by
// range checks, rather than walking every instruction for every candidate.
struct PackingLayout {
  struct Block {
    unsigned Bytes = 0;
    SmallVector<std::pair<MachineInstr *, unsigned>, 4> TrackedOffsets;
  };
  DenseMap<const MachineBasicBlock *, Block> Blocks;
  SmallVector<std::pair<MachineInstr *, MachineBasicBlock *>, 16> Branches;
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
  DenseMap<const MachineBasicBlock *, unsigned> BlockOffsets;
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
    case RISCC::LINK_S3_CALL32_LITERAL:
    case RISCC::LINK_S3_TAIL32_LITERAL:
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
    Island->setAlignment(Align(4));
    if (Where != MF->begin()) {
      MachineBasicBlock &Prev = *std::prev(Where);
      Island->setSectionID(Prev.getSectionID());
      Island->setIsEndSection(Prev.isEndSection());
      Prev.setIsEndSection(false);
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
    // An explicit branch to Next already skips the island. Only an
    // implicit fallthrough needs a new jump; canFallThrough includes both.
    if (!isIsland(Prev) && Prev.getLogicalFallThrough())
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
      // A noreturn call can end a block without a barrier instruction.
      // With no CFG successor, execution cannot fall through into the pool.
      if (!MBB.succ_empty() && (Last == MBB.end() || !Last->isBarrier()))
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

  unsigned computeOffsets(const SmallPtrSetImpl<MachineInstr *> *Skip = nullptr,
                          const PoolPacking *Packing = nullptr,
                          const PackingLayout *Cached = nullptr) {
    // With known instruction sizes and sufficient function alignment these
    // are exact offsets. Otherwise every interval includes maximum padding,
    // so subtracting offsets conservatively bounds its instruction span.
    assert((!Cached || (Packing && !Skip && ExactLayout)) &&
           "only exact packing layouts may use cached offsets");
    if (!Cached) {
      Align SectionAlignment = MF->getAlignment();
      ExactLayout = llvm::all_of(*MF, [&](const MachineBasicBlock &MBB) {
        if (MBB.isBeginSection() && &MBB != &MF->front())
          SectionAlignment = MBB.getAlignment();
        return MBB.getAlignment() <= SectionAlignment &&
               llvm::none_of(MBB, [](const MachineInstr &MI) {
                 // An empty asm may be a memory barrier, but emits no bytes.
                 // Keep it in place while permitting exact layout accounting.
                 return (MI.isInlineAsm() &&
                         MI.getOperand(0).getSymbolName()[0] != '\0') ||
                        MI.isBundle();
               });
      });
    }
    unsigned Offset = 0, Total = 0;
    Offsets.clear();
    BlockOffsets.clear();
    auto EmitPackedWords = [&](const PoolPacking::Placement &P) {
      Offset = alignTo(Offset, 4);
      for (MachineInstr *Word : P.Words) {
        Offsets[Word] = Offset;
        Offset += 4;
      }
    };
    for (MachineBasicBlock &MBB : *MF) {
      if (MBB.isBeginSection()) {
        Total += Offset;
        Offset = 0;
      }
      if (Packing && llvm::is_contained(Packing->Sources, &MBB)) {
        auto P = llvm::find_if(Packing->Placements, [&](const auto &P) {
          return P.Destination == &MBB;
        });
        if (P != Packing->Placements.end()) {
          EmitPackedWords(*P);
          BlockOffsets[&MBB] = Offsets.lookup(P->Words.front());
        } else if (!Packing->removes(MBB)) {
          Offset = alignBlock(Offset, MBB);
          BlockOffsets[&MBB] = Offset;
          for (MachineInstr &MI : MBB)
            if (!Packing->Relocated.contains(&MI)) {
              Offsets[&MI] = Offset;
              Offset += 4;
            }
        }
        continue;
      }
      // Removing the last word removes the island and its alignment too.
      if (Skip && isIsland(MBB) && llvm::all_of(MBB, [&](MachineInstr &MI) {
            return Skip->contains(&MI);
          }))
        continue;
      Offset = alignBlock(Offset, MBB);
      BlockOffsets[&MBB] = Offset;
      if (Cached) {
        const PackingLayout::Block &Block = Cached->Blocks.find(&MBB)->second;
        for (auto [MI, Relative] : Block.TrackedOffsets)
          Offsets[MI] = Offset + Relative;
        Offset += Block.Bytes;
        for (MachineInstr *Jump : Packing->RemovedJumps)
          if (Jump->getParent() == &MBB) {
            assert(&*MBB.getLastNonDebugInstr() == Jump &&
                   "removed island skip must be the final instruction");
            Offset -= TII.getInstSizeInBytes(*Jump);
          }
      } else {
        for (MachineInstr &MI : MBB) {
          Offsets[&MI] = Offset;
          if ((!Skip || !Skip->contains(&MI)) &&
              (!Packing || !Packing->RemovedJumps.contains(&MI)))
            Offset += TII.getInstSizeInBytes(MI);
        }
      }
      if (Packing)
        for (const auto &P : Packing->Placements)
          if (&MBB == P.After)
            EmitPackedWords(P);
    }
    return Total + Offset;
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
      bool NeedsJump = Prev.getLogicalFallThrough() != nullptr;
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

  bool canShareValue(unsigned Value) const {
    // Keep independently relaxable call words and branch-repair words intact.
    return !Values[Value].isMBB() &&
           llvm::none_of(AllUses, [&](const LiteralUse &Use) {
             return Use.ValueIndex == Value &&
                    Use.MI->getOpcode() != RISCC::LDPC;
           });
  }

  // Assign words to the available aligned slots by their users' reach
  // intervals. Earliest-deadline scheduling works across disjoint pools too:
  // a flexible word must not occupy a slot needed by one with a tighter reach.
  bool checkPacking(PoolPacking &Packing, const PackingLayout &Layout,
                    unsigned CurrentBytes) {
    Packing.Relocated.clear();
    SmallVector<MachineInstr *, 16> Words;
    for (const auto &P : Packing.Placements)
      Words.append(P.Words);
    Packing.Relocated.insert_range(Words);
    for (auto [Victim, Replacement] : Packing.Redirects)
      Packing.Relocated.insert(Victim);
    Packing.RemovedJumps.clear();
    for (MachineBasicBlock *Island : Packing.Sources) {
      if (!Packing.removes(*Island) || Island->isEndSection())
        continue;
      auto *Prev = Island->getPrevNode();
      auto Last = Prev->getLastNonDebugInstr();
      if (Last != Prev->end() && Last->getOpcode() == RISCC::JMP8 &&
          Last->getOperand(0).getMBB() == Island->getNextNode())
        Packing.RemovedJumps.insert(&*Last);
    }
    Packing.Bytes = computeOffsets(nullptr, &Packing, &Layout);
    if (Packing.Bytes > CurrentBytes ||
        (Packing.Bytes == CurrentBytes && Packing.RemovedJumps.empty()))
      return false;
    DenseMap<MachineInstr *, std::pair<int64_t, int64_t>> Intervals;
    for (MachineInstr *Word : Words)
      Intervals[Word] = {INT64_MIN, INT64_MAX};
    auto Target = [&](const LiteralUse &Use) {
      auto *Label = Use.MI->getOperand(literalOperand(*Use.MI)).getMCSymbol();
      MachineInstr *Entry = Symbols.lookup(Label);
      auto It = Packing.Redirects.find(Entry);
      return It == Packing.Redirects.end() ? Entry : It->second;
    };
    for (const LiteralUse &Use : AllUses) {
      auto It = Intervals.find(Target(Use));
      if (It == Intervals.end())
        continue;
      int64_t Offset = Offsets.lookup(Use.MI);
      It->second.first = std::max(It->second.first, Offset - 254);
      It->second.second = std::min(It->second.second, Offset + 256);
    }
    SmallVector<std::pair<unsigned, unsigned>, 16> Slots;
    for (auto [I, P] : enumerate(Packing.Placements)) {
      for (MachineInstr *Word : P.Words)
        Slots.emplace_back(Offsets.lookup(Word), I);
      P.Words.clear();
    }
    llvm::sort(Slots);
    for (auto [Slot, Placement] : Slots) {
      MachineInstr *Best = nullptr;
      for (MachineInstr *Word : Words) {
        auto It = Intervals.find(Word);
        if (It != Intervals.end() && It->second.first <= Slot &&
            (!Best || It->second.second < Intervals.lookup(Best).second))
          Best = Word;
      }
      if (!Best || Intervals.lookup(Best).second < Slot)
        return false;
      Intervals.erase(Best);
      Offsets[Best] = Slot;
      Packing.Placements[Placement].Words.push_back(Best);
    }
    for (const LiteralUse &Use : AllUses)
      if (!inRange(*Use.MI, *Target(Use)))
        return false;
    for (auto [MI, Dest] : Layout.Branches)
      if (!Packing.RemovedJumps.contains(MI))
        // Empty forwarding blocks still have an aligned label address.
        if (!MI->getParent()->sameSection(Dest) ||
            !TII.isBranchOffsetInRange(MI->getOpcode(),
                                       int64_t(BlockOffsets.lookup(Dest)) -
                                           Offsets.lookup(MI)))
          return false;
    return true;
  }

  void applyPacking(const PoolPacking &Packing) {
    for (LiteralUse &Use : AllUses) {
      auto *Label = Use.MI->getOperand(literalOperand(*Use.MI)).getMCSymbol();
      auto It = Packing.Redirects.find(Symbols.lookup(Label));
      if (It != Packing.Redirects.end())
        assign(*Use.MI, *It->second);
    }
    SmallPtrSet<MachineBasicBlock *, 2> Destinations;
    for (const auto &P : Packing.Placements) {
      MachineBasicBlock *Destination = P.Destination;
      if (!Destination)
        Destination = &insertIsland(std::next(P.After->getIterator()));
      Destinations.insert(Destination);
      for (MachineInstr *Word : P.Words)
        Destination->splice(Destination->end(), Word->getParent(),
                            Word->getIterator());
    }
    llvm::erase_if(Entries, [&](auto Entry) {
      if (!Packing.Redirects.contains(Entry.first))
        return false;
      Symbols.erase(Entry.first->getOperand(0).getMCSymbol());
      Entry.first->eraseFromParent();
      return true;
    });
    for (MachineBasicBlock *Island : Packing.Sources) {
      if (Destinations.contains(Island) || !Island->empty())
        continue;
      Island->getPrevNode()->setIsEndSection(Island->isEndSection());
      Island->eraseFromParent();
    }
    // Delete only the jumps included in the checked layout. Inferring further
    // fallthroughs here could change alignment or push another load out of
    // range.
    for (MachineInstr *Jump : Packing.RemovedJumps)
      Jump->eraseFromParent();
    unsigned ActualBytes = computeOffsets();
    assert(ActualBytes == Packing.Bytes && "packed layout size mismatch");
    (void)ActualBytes;
  }

  // Revisit complete pools, not just equal words: a safe gap is not necessarily
  // the cheapest gap. Try either existing site and unused natural gaps,
  // counting all padding and obsolete skip jumps in the prospective function
  // layout. A single island can also move to a better gap. Every step strictly
  // reduces bytes or removes a jump, without introducing jumps, so the search
  // terminates.
  bool packReachablePools() {
    // Static bytes and skip counts are a size model, not an estimate of
    // instruction-cache or branch-prediction cost for speed-optimized code.
    if (!EnablePackedLiterals || !MF->getFunction().hasOptSize())
      return false;
    unsigned Bytes = computeOffsets();
    if (!ExactLayout)
      return false;
    // Candidate layouts only move or remove existing entries. Cache immutable
    // value/ownership information instead of rescanning all uses at each site.
    DenseMap<MachineInstr *, unsigned> EntryValues;
    SmallVector<bool, 16> Shareable(Values.size(), true);
    for (auto [Entry, Value] : Entries) {
      EntryValues[Entry] = Value;
      if (Values[Value].isMBB())
        Shareable[Value] = false;
    }
    for (const LiteralUse &Use : AllUses)
      if (Use.MI->getOpcode() != RISCC::LDPC)
        Shareable[Use.ValueIndex] = false;
    auto CoalesceWords = [&](PoolPacking &Packing,
                             SmallVectorImpl<MachineInstr *> &Words) {
      DenseMap<unsigned, MachineInstr *> Representatives;
      llvm::erase_if(Words, [&](MachineInstr *Word) {
        unsigned Value = EntryValues.lookup(Word);
        if (!Shareable[Value])
          return false;
        auto [It, Inserted] = Representatives.try_emplace(Value, Word);
        if (!Inserted)
          Packing.Redirects[Word] = It->second;
        return !Inserted;
      });
    };
    MachineDominatorTree MDT(*MF);
    MachineLoopInfo Loops(MDT);
    auto DepthAfter = [&](MachineBasicBlock *MBB) {
      while (isIsland(*MBB))
        MBB = MBB->getPrevNode();
      return Loops.getLoopDepth(MBB);
    };
    bool Changed = false;
    while (true) {
      // Candidate checks overwrite Offsets. Keep the current layout and each
      // word's outermost users for a cheap, conservative reach rejection.
      computeOffsets();
      struct Position {
        unsigned Offset, Padding;
      };
      struct Bounds {
        Position First{UINT_MAX, 0}, Last{0, 0};
      };
      DenseMap<MachineBasicBlock *, Position> Starts, Ends;
      PackingLayout Layout;
      unsigned End = 0, Padding = 0;
      for (MachineBasicBlock &MBB : *MF) {
        if (MBB.isBeginSection())
          End = Padding = 0;
        unsigned Start = BlockOffsets.lookup(&MBB);
        Padding += Start - End;
        Starts[&MBB] = {Start, Padding};
        End = Start;
        if (!MBB.empty())
          End =
              Offsets.lookup(&MBB.back()) + TII.getInstSizeInBytes(MBB.back());
        Ends[&MBB] = {End, Padding};
        PackingLayout::Block &Block = Layout.Blocks[&MBB];
        Block.Bytes = End - Start;
        for (MachineInstr &MI : MBB) {
          auto *Dest = TII.getBranchDestBlock(MI);
          if (Dest)
            Layout.Branches.emplace_back(&MI, Dest);
          if (Dest || literalOperand(MI) >= 0 ||
              MI.getOpcode() == RISCC::CONSTPOOL_ENTRY)
            Block.TrackedOffsets.emplace_back(&MI, Offsets.lookup(&MI) - Start);
        }
      }
      DenseMap<MachineInstr *, Bounds> UserBounds;
      for (const LiteralUse &Use : AllUses) {
        auto *Label = Use.MI->getOperand(literalOperand(*Use.MI)).getMCSymbol();
        Bounds &Range = UserBounds[Symbols.lookup(Label)];
        Position At{Offsets.lookup(Use.MI),
                    Starts.lookup(Use.MI->getParent()).Padding};
        if (At.Offset < Range.First.Offset)
          Range.First = At;
        if (At.Offset >= Range.Last.Offset)
          Range.Last = At;
      }
      SmallVector<MachineBasicBlock *, 16> Islands, Gaps;
      for (MachineBasicBlock &MBB : *MF) {
        if (isIsland(MBB)) {
          if (MBB.pred_empty() && !MBB.hasAddressTaken() &&
              llvm::all_of(MBB, [](const MachineInstr &MI) {
                return MI.getOpcode() == RISCC::CONSTPOOL_ENTRY;
              }))
            Islands.push_back(&MBB);
          continue;
        }
        auto Last = MBB.getLastNonDebugInstr();
        if ((!MBB.getNextNode() || !isIsland(*MBB.getNextNode())) &&
            (MBB.succ_empty() || (Last != MBB.end() && Last->isBarrier())))
          Gaps.push_back(&MBB);
      }
      std::optional<PoolPacking> Best;
      for (auto [I, First] : enumerate(Islands)) {
        for (unsigned J = I; J != Islands.size(); ++J) {
          MachineBasicBlock *Second = J == I ? nullptr : Islands[J];
          if (Second && !First->sameSection(Second))
            continue;
          PoolPacking Separate{First, Second};
          auto &SeparateWords = Separate.Placements.front().Words;
          for (MachineBasicBlock *Island : Separate.Sources) {
            for (MachineInstr &MI : *Island)
              SeparateWords.push_back(&MI);
          }
          auto Try = [&](ArrayRef<MachineInstr *> Selected,
                         MachineBasicBlock *Destination,
                         MachineBasicBlock *After, bool RequireSharing) {
            unsigned Depth =
                DepthAfter(After ? After : Destination->getPrevNode());
            PoolPacking Unshared{First, Second};
            auto &UnsharedPlacement = Unshared.Placements.front();
            UnsharedPlacement.Words.append(Selected.begin(), Selected.end());
            UnsharedPlacement.Destination = Destination;
            UnsharedPlacement.After = After;
            // Entries already at the destination participate in ordering and
            // sharing too, but unrelated entries in source pools stay put.
            if (Destination) {
              if (!llvm::is_contained(Unshared.Sources, Destination))
                Unshared.Sources.push_back(Destination);
              for (MachineInstr &MI : *Destination)
                if (!llvm::is_contained(UnsharedPlacement.Words, &MI))
                  UnsharedPlacement.Words.push_back(&MI);
            }
            PoolPacking Candidate = Unshared;
            if (EnableSharedLiterals)
              CoalesceWords(Candidate, Candidate.Placements.front().Words);
            if (!checkPacking(Candidate, Layout, Bytes)) {
              if (RequireSharing || Candidate.Redirects.empty())
                return;
              Candidate = Unshared;
              if (!checkPacking(Candidate, Layout, Bytes))
                return;
            }
            Candidate.LoopDepth = Depth;
            unsigned Skips = Candidate.RemovedJumps.size();
            if (!Best || Candidate.Bytes < Best->Bytes ||
                (Candidate.Bytes == Best->Bytes &&
                 (Skips > Best->RemovedJumps.size() ||
                  (Skips == Best->RemovedJumps.size() &&
                   Depth < Best->LoopDepth))))
              Best = std::move(Candidate);
          };
          auto TrySites = [&](ArrayRef<MachineInstr *> Selected,
                              bool RequireSharing) {
            Bounds Range;
            for (MachineInstr *Word : Selected) {
              Bounds Users = UserBounds.lookup(Word);
              if (Users.First.Offset < Range.First.Offset)
                Range.First = Users.First;
              if (Users.Last.Offset >= Range.Last.Offset)
                Range.Last = Users.Last;
            }
            auto MayReach = [&](Position Site, unsigned ExtraWords) {
              // Moving/deleting words can shorten a use-to-site span by at
              // most their original bytes, removed skips, and existing
              // padding within that span. Include a possible new alignment
              // too. This deliberately overestimates the improvement; the
              // exact layout and every branch/load still get checked below.
              int64_t Words = Selected.size() + ExtraWords;
              int64_t Removed = 4 * Words + 2 * (Separate.Sources.size() + 2);
              auto PaddingBetween = [&](Position Use) {
                return std::abs(int64_t(Site.Padding) - Use.Padding);
              };
              return int64_t(Site.Offset) - Range.First.Offset <=
                         256 + Removed + PaddingBetween(Range.First) &&
                     int64_t(Range.Last.Offset) - Site.Offset <=
                         254 + 4 * (Words - 1) + Removed +
                             PaddingBetween(Range.Last);
            };
            for (MachineBasicBlock *Destination : Islands)
              if (First->sameSection(Destination) &&
                  MayReach(Starts.lookup(Destination), Destination->size()))
                Try(Selected, Destination, nullptr, RequireSharing);
            for (MachineBasicBlock *Gap : Gaps)
              if (First->sameSection(Gap) && MayReach(Ends.lookup(Gap), 0))
                Try(Selected, nullptr, Gap, RequireSharing);
          };
          TrySites(SeparateWords, false);
          // Two copies can have a common reachable interval even though neither
          // current address lies in it. Moving just that group permits sharing
          // without dragging along other words that pin the original pools.
          if (Second && EnableSharedLiterals)
            for (MachineInstr &A : *First) {
              unsigned Value = EntryValues.lookup(&A);
              if (!Shareable[Value])
                continue;
              for (MachineInstr &B : *Second)
                if (Value == EntryValues.lookup(&B))
                  TrySites({&A, &B}, true);
            }
        }
      }
      // A pool can be pinned as a whole even though its individual words can
      // occupy different natural gaps. Try evacuating one source into a pair
      // of sites only after the cheaper one-site search has stopped. For each
      // division of slot counts the exact layout is known before assigning
      // words: checkPacking schedules all words across both sets of slots.
      if (!Best && EnableEvacuatedLiterals) {
        SmallVector<PoolPacking::Placement, 16> Sites;
        for (MachineBasicBlock *Island : Islands)
          Sites.push_back({Island, nullptr, {}});
        for (MachineBasicBlock *Gap : Gaps)
          Sites.push_back({nullptr, Gap, {}});
        auto SiteBlock = [](const PoolPacking::Placement &P) {
          return P.Destination ? P.Destination : P.After;
        };
        for (MachineBasicBlock *Source : Islands) {
          if (Source->size() < 2)
            continue;
          for (auto [I, Left] : enumerate(Sites)) {
            if (SiteBlock(Left) == Source ||
                !Source->sameSection(SiteBlock(Left)))
              continue;
            for (const auto &Right : ArrayRef(Sites).drop_front(I + 1)) {
              if (SiteBlock(Right) == Source ||
                  !Source->sameSection(SiteBlock(Right)))
                continue;
              PoolPacking Base{Source, nullptr};
              Base.Placements = {Left, Right};
              SmallVector<MachineInstr *, 16> Words;
              for (MachineInstr &MI : *Source)
                Words.push_back(&MI);
              for (const auto &P : Base.Placements)
                if (P.Destination) {
                  Base.Sources.push_back(P.Destination);
                  for (MachineInstr &MI : *P.Destination)
                    Words.push_back(&MI);
                }
              // Far-away sites cannot become reachable merely by moving these
              // words. Bound the maximum shortening using their bytes, skips
              // and the existing padding; the exact check still follows. Do
              // not limit the number of pools or disable large functions.
              auto MayReach = [&](MachineInstr *Word,
                                  const PoolPacking::Placement &P) {
                Position Site = P.Destination ? Starts.lookup(P.Destination)
                                              : Ends.lookup(P.After);
                Bounds Users = UserBounds.lookup(Word);
                int64_t Count = Words.size();
                int64_t Removed = 4 * Count + 2 * (Base.Sources.size() + 2);
                return int64_t(Site.Offset) - Users.First.Offset <=
                           256 + Removed +
                               std::abs(int64_t(Site.Padding) -
                                        Users.First.Padding) &&
                       int64_t(Users.Last.Offset) - Site.Offset <=
                           254 + 4 * (Count - 1) + Removed +
                               std::abs(int64_t(Site.Padding) -
                                        Users.Last.Padding);
              };
              if (llvm::any_of(Words, [&](MachineInstr *Word) {
                    return !MayReach(Word, Left) && !MayReach(Word, Right);
                  }))
                continue;
              for (bool Share : {true, false}) {
                if (Share && !EnableSharedLiterals)
                  continue;
                PoolPacking Template = Base;
                SmallVector<MachineInstr *, 16> Kept = Words;
                if (Share) {
                  CoalesceWords(Template, Kept);
                  if (Template.Redirects.empty())
                    continue;
                }
                for (unsigned Count = 1; Count < Kept.size(); ++Count) {
                  PoolPacking Candidate = Template;
                  Candidate.Placements[0].Words.append(Kept.begin(),
                                                       Kept.begin() + Count);
                  Candidate.Placements[1].Words.append(Kept.begin() + Count,
                                                       Kept.end());
                  if (!checkPacking(Candidate, Layout, Bytes))
                    continue;
                  unsigned Skips = Candidate.RemovedJumps.size();
                  Candidate.LoopDepth = std::max(DepthAfter(SiteBlock(Left)),
                                                 DepthAfter(SiteBlock(Right)));
                  if (!Best || Candidate.Bytes < Best->Bytes ||
                      (Candidate.Bytes == Best->Bytes &&
                       (Skips > Best->RemovedJumps.size() ||
                        (Skips == Best->RemovedJumps.size() &&
                         Candidate.LoopDepth < Best->LoopDepth))))
                    Best = std::move(Candidate);
                }
              }
            }
          }
        }
        if (Best)
          LLVM_DEBUG(dbgs() << "Evacuated pool in " << MF->getName() << ": "
                            << Bytes << " -> " << Best->Bytes << " bytes, "
                            << Best->RemovedJumps.size() << " skips removed\n");
      }
      if (!Best)
        break;
      applyPacking(*Best);
      Bytes = Best->Bytes;
      Changed = true;
    }
    computeOffsets();
    return Changed;
  }

  // A forward load can pin an island between natural gaps. Try delaying its
  // dependency chain or moving the load earlier over independent instructions,
  // then let the full packing checks decide whether that opens a cheaper
  // layout. Never retain a schedule change without a packing gain.
  bool moveLiteralsForPacking() {
    if (!EnableLiteralMotion || !EnablePackedLiterals ||
        !MF->getFunction().hasOptSize())
      return false;
    const auto &TRI = *MF->getSubtarget<RISCCSubtarget>().getRegisterInfo();
    auto Schedulable = [](const MachineInstr &MI) {
      return !MI.isCall() && !MI.isTerminator() && !MI.isInlineAsm() &&
             !MI.isMetaInstruction() && !MI.isPosition() && !MI.isBundle() &&
             !MI.isBundled() && !MI.getPreInstrSymbol() &&
             !MI.getPostInstrSymbol() && !MI.hasUnmodeledSideEffects() &&
             !MI.hasOrderedMemoryRef() && !MI.mayRaiseFPException() &&
             !MI.getFlag(MachineInstr::FrameSetup) &&
             !MI.getFlag(MachineInstr::FrameDestroy) &&
             llvm::none_of(MI.operands(), [](const MachineOperand &MO) {
               return MO.isRegMask();
             });
    };
    auto Independent = [&](const MachineInstr &A, const MachineInstr &B) {
      for (const MachineOperand &MO : A.operands()) {
        if (!MO.isReg() || !MO.getReg())
          continue;
        for (const MachineOperand &Other : B.operands())
          if (Other.isReg() && Other.getReg() &&
              (MO.isDef() || Other.isDef()) &&
              TRI.regsOverlap(MO.getReg(), Other.getReg()))
            return false;
      }
      return !A.mayAlias(static_cast<AAResults *>(nullptr), B, false) ||
             A.isDereferenceableInvariantLoad() ||
             B.isDereferenceableInvariantLoad();
    };
    bool Changed = false;
    for (const LiteralUse &Use : AllUses) {
      MachineInstr &Load = *Use.MI;
      if (Load.getOpcode() != RISCC::LDPC || !Schedulable(Load) ||
          Values[Use.ValueIndex].isMBB())
        continue;
      computeOffsets();
      if (!ExactLayout)
        break;
      MachineInstr *Entry = Symbols.lookup(Load.getOperand(1).getMCSymbol());
      if (Offsets.lookup(&Load) >= Offsets.lookup(Entry))
        continue;
      MachineBasicBlock *Island = Entry->getParent();
      MachineBasicBlock *Prev = Island->getPrevNode();
      if (!Prev || Island->isEndSection())
        continue;
      auto Skip = Prev->getLastNonDebugInstr();
      if (Skip == Prev->end() || Skip->getOpcode() != RISCC::JMP8 ||
          Skip->getOperand(0).getMBB() != Island->getNextNode())
        continue;

      MachineBasicBlock &MBB = *Load.getParent();
      SmallVector<MachineInstr *, 32> Original;
      for (MachineInstr &MI : MBB)
        Original.push_back(&MI);
      auto RestoreOrder = [&] {
        for (MachineInstr *MI : Original)
          MBB.splice(MBB.end(), &MBB, MI->getIterator());
      };
      auto TrySinking = [&](bool Together) {
        SmallVector<MachineInstr *, 16> Dependent{&Load};
        unsigned Examined = 0, Moved = 0, Roots = 1;
        for (auto I = std::next(Load.getIterator());
             I != MBB.end() && Examined++ < 32;) {
          MachineInstr &MI = *I++;
          if (!Schedulable(MI))
            break;
          // Several loads can pin the same island. Treat their dependency
          // chains as one unit instead of moving a second load ahead of the
          // first, which cannot improve the earliest user's reach.
          if (Together && MI.getOpcode() == RISCC::LDPC &&
              Symbols.lookup(MI.getOperand(1).getMCSymbol())->getParent() ==
                  Island) {
            Dependent.push_back(&MI);
            ++Roots;
          } else if (llvm::all_of(Dependent, [&](const MachineInstr *Blocked) {
                       return Independent(MI, *Blocked);
                     })) {
            MBB.splice(Load.getIterator(), &MBB, MI.getIterator());
            ++Moved;
          } else {
            Dependent.push_back(&MI);
          }
        }
        if (Moved && (!Together || Roots > 1) && packReachablePools()) {
          // Read/read reordering can change which use kills a physical
          // register.
          recomputeLivenessFlags(MBB);
          return true;
        }
        RestoreOrder();
        return false;
      };
      // The load can instead be just beyond the preceding island's backward
      // reach. Moving it earlier may let packing absorb its word there and
      // remove the forward island. Cross only independent instructions in
      // this block, and keep the motion only after checking the whole layout.
      auto TryHoisting = [&] {
        unsigned Moved = 0;
        while (Load.getIterator() != MBB.begin() && Moved < 32) {
          MachineInstr &Prev = *std::prev(Load.getIterator());
          if (!Schedulable(Prev) || !Independent(Prev, Load))
            break;
          MBB.splice(Prev.getIterator(), &MBB, Load.getIterator());
          ++Moved;
        }
        if (Moved && packReachablePools()) {
          recomputeLivenessFlags(MBB);
          return true;
        }
        RestoreOrder();
        return false;
      };
      if (TrySinking(false) || TrySinking(true) ||
          (EnableLiteralHoisting && TryHoisting()))
        Changed = true;
    }
    computeOffsets();
    return Changed;
  }

  // Initial placement treats each natural gap as a separate region. A word
  // in an older region can still be reachable, even when an intervening gap
  // received a new copy. Merge those copies after branch relaxation, checking
  // the entire prospective layout (including alignment and island skips).
  bool mergeReachableEntries() {
    if (!EnableSharedLiterals || !MF->getFunction().hasOptSize())
      return false;
    computeOffsets();
    if (!ExactLayout)
      return false;
    bool Changed = false;
    while (true) {
      MachineInstr *Victim = nullptr, *Replacement = nullptr;
      for (auto [Entry, Value] : llvm::reverse(Entries)) {
        // Let the linker relax call-target words independently; sharing one
        // can prevent their removal. Branch-address repair is also separate.
        if (!canShareValue(Value))
          continue;
        auto *Label = Entry->getOperand(0).getMCSymbol();
        for (auto [Other, OtherValue] : Entries) {
          if (Other == Entry || OtherValue != Value ||
              Other->getParent()->getSectionID() !=
                  Entry->getParent()->getSectionID())
            continue;
          SmallPtrSet<MachineInstr *, 2> Removed{Entry};
          MachineBasicBlock &Island = *Entry->getParent();
          if (Island.size() == 1) {
            auto *Prev = Island.getPrevNode();
            auto Last = Prev->getLastNonDebugInstr();
            if (!Island.isEndSection() && Last != Prev->end() &&
                Last->getOpcode() == RISCC::JMP8 &&
                Last->getOperand(0).getMBB() == Island.getNextNode())
              Removed.insert(&*Last);
          }
          computeOffsets(&Removed);
          bool Fits = llvm::all_of(AllUses, [&](const LiteralUse &Use) {
            auto *Target =
                Use.MI->getOperand(literalOperand(*Use.MI)).getMCSymbol();
            return inRange(*Use.MI,
                           Target == Label ? *Other : *Symbols.lookup(Target));
          });
          for (MachineBasicBlock &MBB : *MF)
            for (MachineInstr &MI : MBB)
              if (!Removed.contains(&MI))
                if (auto *Dest = TII.getBranchDestBlock(MI))
                  Fits &= !Dest->empty() &&
                          MBB.getSectionID() == Dest->getSectionID() &&
                          TII.isBranchOffsetInRange(
                              MI.getOpcode(),
                              int64_t(Offsets.lookup(&Dest->front())) -
                                  Offsets.lookup(&MI));
          if (Fits) {
            Victim = Entry;
            Replacement = Other;
            break;
          }
        }
        if (Victim)
          break;
      }
      if (!Victim)
        break;
      auto *Label = Victim->getOperand(0).getMCSymbol();
      for (LiteralUse &Use : AllUses)
        if (Use.MI->getOperand(literalOperand(*Use.MI)).getMCSymbol() == Label)
          assign(*Use.MI, *Replacement);
      removeUnusedEntries();
      computeOffsets();
      Changed = true;
    }
    computeOffsets();
    return Changed;
  }

  // Large leaf functions conservatively reserve a scratch spill for branch
  // relaxation. If it was never used, a frame containing only that slot is
  // unnecessary. Run after relaxation, and check the new literal alignment
  // before removing anything: deleting an ADDI can add two pool-padding bytes.
  bool removeUnusedEmergencyFrame() {
    auto &Info = *MF->getInfo<RISCCMachineFunctionInfo>();
    auto &Frame = MF->getFrameInfo();
    int FI = Info.getBranchRelaxationSpillFI();
    if (FI < 0 || Frame.getStackSize() != 4 || Frame.hasVarSizedObjects())
      return false;
    for (int I = Frame.getObjectIndexBegin(), E = Frame.getObjectIndexEnd();
         I != E; ++I)
      if (I != FI && !Frame.isDeadObjectIndex(I))
        return false;

    const auto &TRI = *MF->getSubtarget().getRegisterInfo();
    SmallPtrSet<MachineInstr *, 8> Adjustments;
    for (MachineBasicBlock &MBB : *MF)
      for (MachineInstr &MI : MBB) {
        if (MI.isCall() || MI.isCFIInstruction() || MI.isInlineAsm() ||
            MI.isBundle())
          return false;
        if (!MI.readsRegister(RISCC::R7, &TRI) &&
            !MI.modifiesRegister(RISCC::R7, &TRI))
          continue;
        if (MI.getOpcode() != RISCC::ADDI32 || MI.getNumExplicitOperands() != 3 ||
            MI.getOperand(0).getReg() != RISCC::R7 ||
            MI.getOperand(1).getReg() != RISCC::R7 ||
            !((MI.getFlag(MachineInstr::FrameSetup) &&
               MI.getOperand(2).getImm() == -4) ||
              (MI.getFlag(MachineInstr::FrameDestroy) &&
               MI.getOperand(2).getImm() == 4)))
          return false;
        Adjustments.insert(&MI);
      }
    if (Adjustments.empty())
      return false;

    computeOffsets(&Adjustments);
    bool Fits = llvm::all_of(AllUses, [&](const LiteralUse &Use) {
      auto *Symbol = Use.MI->getOperand(literalOperand(*Use.MI)).getMCSymbol();
      return inRange(*Use.MI, *Symbols.lookup(Symbol));
    });
    for (MachineBasicBlock &MBB : *MF)
      for (MachineInstr &MI : MBB)
        if (auto *Dest = TII.getBranchDestBlock(MI))
          Fits &= !Dest->empty() && MBB.getSectionID() == Dest->getSectionID() &&
                  TII.isBranchOffsetInRange(
                      MI.getOpcode(), int64_t(Offsets.lookup(&Dest->front())) -
                                          Offsets.lookup(&MI));
    if (!Fits) {
      computeOffsets();
      return false;
    }
    for (MachineInstr *MI : Adjustments)
      MI->eraseFromParent();
    Frame.RemoveStackObject(FI);
    Frame.setStackSize(0);
    Info.setBranchRelaxationSpillFI(-1);
    return true;
  }

public:
  explicit RISCCConstantIslands(MachineFunction &MF)
      : MF(&MF), TII(*MF.getSubtarget<RISCCSubtarget>().getInstrInfo()) {}

  bool run() {
    bool Changed = removeForwardingBlocks(*MF);
    Changed |= shortenExitBranches(*MF);
    if (!MF->getSubtarget<RISCCSubtarget>().isRC32())
      return Changed;
    // Direct-call symbols remain visible to IPRA until this final pass.
    for (MachineBasicBlock &MBB : *MF)
      for (MachineInstr &MI : MBB) {
        if (MI.getOpcode() != RISCC::CALL32_LITERAL &&
            MI.getOpcode() != RISCC::TAIL32_LITERAL &&
            MI.getOpcode() != RISCC::LINK_S3_CALL32_LITERAL &&
            MI.getOpcode() != RISCC::LINK_S3_TAIL32_LITERAL)
          continue;
        MachineOperand &Target = MI.getOperand(0);
        RISCCConstantPoolSymbol *Value;
        if (Target.isGlobal()) {
          assert(Target.getOffset() == 0 && "offset on direct call target");
          Value = RISCCConstantPoolSymbol::Create(
              MF->getFunction().getContext(), Target.getGlobal(), false, true);
        } else {
          assert(Target.isSymbol() && "unexpected direct call target");
          Value = RISCCConstantPoolSymbol::CreateCallTarget(
              MF->getFunction().getContext(), Target.getSymbolName());
        }
        unsigned Index =
            MF->getConstantPool()->getConstantPoolIndex(Value, Align(4));
        Target.ChangeToCPI(Index, 0);
      }
    planLiteralPools();
    if (!AllUses.empty())
      MF->ensureAlignment(Align(4));
    materializePools();
    Changed |= !AllUses.empty();
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
    Changed |= removeUnusedEmergencyFrame();
    Changed |= packReachablePools();
    Changed |= mergeReachableEntries();
    Changed |= moveLiteralsForPacking();
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
