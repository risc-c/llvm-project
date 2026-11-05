//===-- RISCCAdds.cpp - Place and fold S-register addends -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCInstrInfo.h"
#include "RISCCMachineOptimize.h"
#include "RISCCSRegLiveness.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "riscc-adds"

static cl::opt<bool> EnableAddsPlacement(
    "riscc-adds-placement", cl::Hidden, cl::init(true),
    cl::desc("Place addend live ranges in S registers before GPR allocation"));
static cl::opt<bool> EnableAddsMixedUses(
    "riscc-adds-mixed-uses", cl::Hidden, cl::init(true),
    cl::desc("Allow profitable GPR reloads within an S-register live range"));

RISCCSRegLiveness::RISCCSRegLiveness(const MachineFunction &MF,
                                     const TargetRegisterInfo &TRI,
                                     MCRegister Ignore) {
  SmallVector<unsigned, 16> LiveIn(MF.getNumBlockIDs(), 0);
  SmallVector<unsigned, 16> LiveOut(LiveIn.size(), 0);
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      Access &A = Instructions[&MI];
      bool UnknownCall =
          MI.isCall() && llvm::none_of(MI.operands(), [](const auto &MO) {
            return MO.isRegMask();
          });
      for (unsigned I = 0; I != Registers.size(); ++I) {
        MCRegister Reg = Registers[I];
        if (Reg == Ignore)
          continue;
        if (MI.readsRegister(Reg, &TRI))
          A.Uses |= 1u << I;
        if (UnknownCall || MI.modifiesRegister(Reg, &TRI))
          A.Defs |= 1u << I;
      }
    }
  bool Changed;
  do {
    Changed = false;
    for (const MachineBasicBlock &MBB : llvm::reverse(MF)) {
      unsigned Live = 0;
      for (const MachineBasicBlock *Succ : MBB.successors())
        Live |= LiveIn[Succ->getNumber()];
      LiveOut[MBB.getNumber()] = Live;
      for (const MachineInstr &MI : llvm::reverse(MBB)) {
        Access A = Instructions.lookup(&MI);
        Live = (Live & ~A.Defs) | A.Uses;
      }
      if (LiveIn[MBB.getNumber()] != Live) {
        LiveIn[MBB.getNumber()] = Live;
        Changed = true;
      }
    }
  } while (Changed);
  for (const MachineBasicBlock &MBB : MF) {
    unsigned Live = LiveOut[MBB.getNumber()];
    for (const MachineInstr &MI : llvm::reverse(MBB)) {
      Access &A = Instructions[&MI];
      A.After = Live;
      Live = (Live & ~A.Defs) | A.Uses;
      A.Before = Live;
    }
  }
}

// Share the legality check between transfer folding and spill allocation.
// A stack reload becomes MFS if the allocator places its slot in an S register.
// Count that opportunity only when all uses can read the unchanged source.
bool llvm::findRISCCAddsUsers(MachineInstr &Def,
                              SmallVectorImpl<MachineInstr *> &Uses,
                              SmallVectorImpl<MachineInstr *> &DebugUses) {
  const auto &STI = Def.getMF()->getSubtarget<RISCCSubtarget>();
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  bool IsMove = Def.getOpcode() == RISCC::MFS;
  if ((!IsMove && (Def.getOpcode() != TII.getNativeOpcode(RISCC::LD) ||
                   !Def.getOperand(1).isFI())) ||
      Def.isBundled() || Def.getFlag(MachineInstr::FrameSetup) ||
      Def.getFlag(MachineInstr::FrameDestroy))
    return false;
  Register Reg = Def.getOperand(0).getReg();
  if (!Reg.isPhysical())
    return false;
  Register SReg = IsMove ? Def.getOperand(1).getReg() : Register();
  int FrameIndex = IsMove ? 0 : Def.getOperand(1).getIndex();
  unsigned Add = TII.getNativeOpcode(RISCC::ADD);
  bool Killed = false;
  bool SourceChanged = false;
  for (auto I = std::next(Def.getIterator()), E = Def.getParent()->instr_end();
       I != E; ++I) {
    MachineInstr &MI = *I;
    if (MI.isDebugInstr()) {
      if (MI.readsRegister(Reg, &TRI)) {
        if (!MI.isDebugValue())
          return false;
        DebugUses.push_back(&MI);
      }
      continue;
    }
    if (MI.isCall() || MI.isInlineAsm() || MI.isBundled() ||
        MI.isCFIInstruction() || MI.hasUnmodeledSideEffects() ||
        MI.getFlag(MachineInstr::FrameSetup) ||
        MI.getFlag(MachineInstr::FrameDestroy))
      return false;
    if (MI.readsRegister(Reg, &TRI)) {
      if (SourceChanged || MI.getOpcode() != Add || MI.getNumOperands() != 3 ||
          (MI.getOperand(1).getReg() == Reg &&
           MI.getOperand(2).getReg() == Reg))
        return false;
      Uses.push_back(&MI);
    }
    if (MI.modifiesRegister(Reg, &TRI)) {
      Killed = true;
      break;
    }
    if (IsMove
            ? MI.modifiesRegister(SReg, &TRI)
            : MI.mayStore() && llvm::any_of(MI.operands(), [&](const auto &MO) {
                return MO.isFI() && MO.getIndex() == FrameIndex;
              }))
      SourceChanged = true;
  }
  LivePhysRegs LiveOut(TRI);
  LiveOut.addLiveOuts(*Def.getParent());
  return !Uses.empty() && (Killed || !LiveOut.contains(Reg));
}

// Keep the GPR in ADD's first source slot. ADDS uses the second MIR source
// for the S register, even though assembly prints that source first.
static void convertToAdds(MachineInstr &MI, Register Reg, Register SReg,
                          const RISCCInstrInfo &TII) {
  if (MI.getOperand(1).getReg() == Reg)
    TII.commuteInstruction(MI);
  MI.setDesc(TII.get(TII.getNativeOpcode(RISCC::ADDS)));
  MI.getOperand(1).setIsKill(false);
  MI.getOperand(2).setReg(SReg);
  MI.getOperand(2).setIsKill(false);
  MI.getOperand(2).setIsRenamable(false);
}

// Fold transfers after allocation, including scheduled and repeated uses.
bool llvm::foldRISCCAdds(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  if (!STI.hasAdds())
    return false;
  const auto &TII = *STI.getInstrInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    bool BlockChanged = false;
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &Move = *I++;
      if (Move.getOpcode() != RISCC::MFS)
        continue;
      Register Reg = Move.getOperand(0).getReg();
      Register SReg = Move.getOperand(1).getReg();
      SmallVector<MachineInstr *, 4> Uses;
      SmallVector<MachineInstr *, 2> DebugUses;
      if (!findRISCCAddsUsers(Move, Uses, DebugUses))
        continue;
      for (MachineInstr *MI : Uses)
        convertToAdds(*MI, Reg, SReg, TII);
      for (MachineInstr *MI : DebugUses)
        MI->setDebugValueUndef();
      Move.eraseFromParent();
      BlockChanged = true;
    }
    if (BlockChanged)
      recomputeLivenessFlags(MBB);
    Changed |= BlockChanged;
  }
  return Changed;
}

namespace {

// Number instruction boundaries so a dying operand and a newly defined value
// can share an S register at the same instruction. GPR pressure includes values
// live through blocks and PHI operands on predecessor edges. For profitability,
// exclude values unused throughout the current loop: the GPR allocator can
// spill those at the loop's boundaries instead of paying on every iteration.
class AddsLiveRanges {
  struct Block {
    unsigned Begin = 0, End = 0;
    BitVector Defs, Uses, PhiOut, LiveIn, LiveOut, Active;
    explicit Block(unsigned Regs)
        : Defs(Regs), Uses(Regs), PhiOut(Regs), LiveIn(Regs), LiveOut(Regs),
          Active(Regs) {}
  };
  SmallVector<Block, 16> Blocks;

public:
  DenseMap<const MachineInstr *, unsigned> Position;
  SmallVector<MachineInstr *, 64> Instructions;
  SmallVector<unsigned, 128> Pressure;

  unsigned blockBegin(const MachineBasicBlock &MBB) const {
    return 2 * Blocks[MBB.getNumber()].Begin;
  }

  bool isActive(Register Reg, const MachineBasicBlock &MBB) const {
    return Blocks[MBB.getNumber()].Active.test(Reg.virtRegIndex());
  }

  AddsLiveRanges(MachineFunction &MF, const RISCCSubtarget &STI,
                 const MachineLoopInfo &MLI) {
    auto &MRI = MF.getRegInfo();
    for (unsigned I = 0; I != MF.getNumBlockIDs(); ++I)
      Blocks.emplace_back(MRI.getNumVirtRegs());
    auto IsGPR = [&](Register Reg) {
      return Reg.isVirtual() &&
             STI.getGPRClass()->hasSubClassEq(MRI.getRegClass(Reg));
    };
    for (MachineBasicBlock &MBB : MF) {
      Block &B = Blocks[MBB.getNumber()];
      B.Begin = Instructions.size();
      for (MachineInstr &MI : MBB) {
        Position[&MI] = Instructions.size();
        Instructions.push_back(&MI);
        if (MI.isDebugInstr())
          continue;
        if (MI.isPHI()) {
          for (unsigned I = 1; I < MI.getNumOperands(); I += 2) {
            const MachineOperand &MO = MI.getOperand(I);
            if (!MO.isUndef() && IsGPR(MO.getReg()))
              Blocks[MI.getOperand(I + 1).getMBB()->getNumber()].PhiOut.set(
                  MO.getReg().virtRegIndex());
          }
        } else {
          for (const MachineOperand &MO : MI.uses())
            if (MO.isReg() && !MO.isUndef() && IsGPR(MO.getReg())) {
              unsigned R = MO.getReg().virtRegIndex();
              if (!B.Defs.test(R))
                B.Uses.set(R);
            }
        }
        for (const MachineOperand &MO : MI.defs())
          if (MO.isReg() && IsGPR(MO.getReg()))
            B.Defs.set(MO.getReg().virtRegIndex());
      }
      B.End = Instructions.size();
    }
    for (MachineBasicBlock &MBB : MF) {
      Block &B = Blocks[MBB.getNumber()];
      auto Include = [&](const MachineBasicBlock *BB) {
        const Block &Other = Blocks[BB->getNumber()];
        B.Active |= Other.Defs;
        B.Active |= Other.Uses;
        B.Active |= Other.PhiOut;
      };
      if (const MachineLoop *Loop = MLI.getLoopFor(&MBB))
        for (const MachineBasicBlock *BB : Loop->blocks())
          Include(BB);
      else
        Include(&MBB);
    }
    // Constants and simple addresses can be rematerialized where they are
    // used. Their presence in another block of the loop does not force a
    // spill there; counting them would overstate the value of an S register.
    BitVector Rematerializable(MRI.getNumVirtRegs());
    for (unsigned R = 0, E = MRI.getNumVirtRegs(); R != E; ++R)
      if (MachineInstr *Def = MRI.getUniqueVRegDef(Register::index2VirtReg(R)))
        if (STI.getInstrInfo()->isTriviallyReMaterializable(*Def))
          Rematerializable.set(R);
    for (Block &B : Blocks) {
      BitVector Local = B.Defs;
      Local |= B.Uses;
      Local |= B.PhiOut;
      B.Active.reset(Rematerializable);
      Local &= Rematerializable;
      B.Active |= Local;
    }
    bool Changed;
    do {
      Changed = false;
      for (MachineBasicBlock &MBB : llvm::reverse(MF)) {
        Block &B = Blocks[MBB.getNumber()];
        BitVector Live = B.PhiOut;
        for (MachineBasicBlock *Succ : MBB.successors())
          Live |= Blocks[Succ->getNumber()].LiveIn;
        B.LiveOut = Live;
        Live.reset(B.Defs);
        Live |= B.Uses;
        if (Live != B.LiveIn) {
          B.LiveIn = std::move(Live);
          Changed = true;
        }
      }
    } while (Changed);
    Pressure.resize(2 * Instructions.size());
    for (MachineBasicBlock &MBB : MF) {
      BitVector Live = Blocks[MBB.getNumber()].LiveOut;
      const BitVector &Active = Blocks[MBB.getNumber()].Active;
      auto CountPressure = [&] {
        BitVector Working = Live;
        Working &= Active;
        return Working.count();
      };
      for (MachineInstr &MI : llvm::reverse(MBB)) {
        unsigned P = 2 * Position.lookup(&MI);
        Pressure[P + 1] = CountPressure();
        if (!MI.isDebugInstr()) {
          for (const MachineOperand &MO : MI.defs())
            if (MO.isReg() && IsGPR(MO.getReg()))
              Live.reset(MO.getReg().virtRegIndex());
          if (!MI.isPHI())
            for (const MachineOperand &MO : MI.uses())
              if (MO.isReg() && !MO.isUndef() && IsGPR(MO.getReg()))
                Live.set(MO.getReg().virtRegIndex());
        }
        Pressure[P] = CountPressure();
      }
    }
  }

  BitVector range(MachineInstr &Def, ArrayRef<MachineInstr *> Uses) const {
    BitVector Result(Pressure.size());
    SmallVector<std::pair<MachineBasicBlock *, unsigned>, 16> Work;
    DenseMap<MachineBasicBlock *, unsigned> Covered;
    for (MachineInstr *Use : Uses)
      Work.emplace_back(Use->getParent(), 2 * Position.lookup(Use) + 1);
    while (!Work.empty()) {
      auto [BB, End] = Work.pop_back_val();
      if (auto I = Covered.find(BB); I != Covered.end() && I->second >= End)
        continue;
      Covered[BB] = End;
      unsigned Begin = BB == Def.getParent()
                           ? 2 * Position.lookup(&Def) + 1
                           : 2 * Blocks[BB->getNumber()].Begin;
      assert(Begin <= End && "definition must dominate its uses");
      Result.set(Begin, End);
      if (BB != Def.getParent())
        for (MachineBasicBlock *Pred : BB->predecessors())
          Work.emplace_back(Pred, 2 * Blocks[Pred->getNumber()].End);
    }
    return Result;
  }
};

} // namespace

// Place addends before GPR allocation chooses spills. An S register belongs to
// a value's live range, so unrelated phases and calls do not reserve it. Mixed
// consumers share local GPR copies, paid for by repeated ADD uses.
bool llvm::placeRISCCAddsOperands(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<RISCCSubtarget>();
  if (!STI.hasAdds() || !EnableAddsPlacement || MF.getFunction().hasOptSize())
    return false;
  auto &MRI = MF.getRegInfo();
  const auto &TII = *STI.getInstrInfo();
  const auto &TRI = *STI.getRegisterInfo();
  unsigned Add = TII.getNativeOpcode(RISCC::ADD);
  MachineDominatorTree MDT(MF);
  MachineLoopInfo MLI(MDT);
  MachineBranchProbabilityInfo MBPI;
  MachineBlockFrequencyInfo MBFI(MF, MBPI, MLI);
  RegisterClassInfo RCI;
  RCI.runOnMachineFunction(MF);
  unsigned Capacity = RCI.getNumAllocatableRegs(STI.getGPRClass());
  AddsLiveRanges Liveness(MF, STI, MLI);
  RISCCSRegLiveness SLive(MF, TRI);
  SmallVector<BitVector, 6> Occupied(RISCCSRegLiveness::Registers.size(),
                                     BitVector(Liveness.Pressure.size()));
  for (MachineInstr *MI : Liveness.Instructions) {
    unsigned P = 2 * Liveness.Position.lookup(MI);
    auto A = SLive.Instructions.lookup(MI);
    for (unsigned I = 0; I != Occupied.size(); ++I) {
      if (A.Before & (1u << I))
        Occupied[I].set(P);
      if ((A.After | A.Defs) & (1u << I))
        Occupied[I].set(P + 1);
    }
  }
  auto IsAddUse = [&](const MachineInstr &MI, Register Reg) {
    return MI.getOpcode() == Add && MI.getNumOperands() == 3 &&
           MI.getOperand(1).getReg() != MI.getOperand(2).getReg() &&
           (MI.getOperand(1).getReg() == Reg ||
            MI.getOperand(2).getReg() == Reg) &&
           !MI.getOperand(1).getSubReg() && !MI.getOperand(2).getSubReg() &&
           !MI.getOperand(1).isUndef() && !MI.getOperand(2).isUndef();
  };
  auto Frequency = [&](const MachineInstr &MI) {
    return MBFI.getBlockFreq(MI.getParent()).getFrequency();
  };
  struct Candidate {
    Register Reg;
    MachineInstr *Def;
    SmallVector<MachineInstr *, 4> Uses;
    SmallVector<MachineInstr *, 2> DebugUses;
    BitVector Range;
    uint64_t Score = 0;
  };
  // Share a single reload between a block's ordinary consumers. Separate
  // copies create unrelated GPR values and can make later allocation spill
  // several copies of the same addend. Keep the defining GPR in its own block.
  auto ReloadRanges = [&](const Candidate &C) {
    DenseMap<MachineBasicBlock *, std::pair<unsigned, unsigned>> Reloads;
    for (MachineInstr *Use : C.Uses) {
      if (IsAddUse(*Use, C.Reg))
        continue;
      unsigned P = 2 * Liveness.Position.lookup(Use);
      auto [I, Inserted] = Reloads.try_emplace(Use->getParent(), P, P + 1);
      if (!Inserted) {
        I->second.first = std::min(I->second.first, P);
        I->second.second = std::max(I->second.second, P + 1);
      }
    }
    if (auto I = Reloads.find(C.Def->getParent()); I != Reloads.end())
      I->second.first = 2 * Liveness.Position.lookup(C.Def) + 1;
    return Reloads;
  };
  auto FreedRange = [&](const Candidate &C) {
    BitVector Freed = C.Range;
    Freed.reset(2 * Liveness.Position.lookup(C.Def) + 1);
    for (auto &[BB, Range] : ReloadRanges(C))
      Freed.reset(Range.first, Range.second);
    return Freed;
  };
  auto Score = [&](const Candidate &C) -> uint64_t {
    uint64_t AddsWeight = 0, Cost = Frequency(*C.Def);
    auto Reloads = ReloadRanges(C);
    if (!EnableAddsMixedUses && !Reloads.empty())
      return 0;
    for (auto &[BB, Range] : Reloads)
      if (BB != C.Def->getParent())
        Cost = SaturatingAdd(Cost, MBFI.getBlockFreq(BB).getFrequency());
    for (MachineInstr *Use : C.Uses) {
      if (IsAddUse(*Use, C.Reg))
        AddsWeight = SaturatingAdd(AddsWeight, Frequency(*Use));
    }
    BitVector Freed = FreedRange(C);
    SmallPtrSet<MachineBasicBlock *, 8> Pressured;
    for (unsigned P : Freed.set_bits()) {
      MachineBasicBlock *BB = Liveness.Instructions[P / 2]->getParent();
      if (Liveness.isActive(C.Reg, *BB) && Liveness.Pressure[P] > Capacity)
        Pressured.insert(BB);
    }
    uint64_t PressureWeight = 0;
    for (MachineBasicBlock *BB : Pressured)
      PressureWeight =
          SaturatingAdd(PressureWeight, MBFI.getBlockFreq(BB).getFrequency());
    // One avoided spill needs a store and a reload. Cap that estimate by the
    // actual ADD uses; a one-use temporary does not justify an extra MTS.
    uint64_t Benefit =
        std::min(AddsWeight, SaturatingMultiply(PressureWeight, uint64_t(2)));
    return Benefit > Cost ? Benefit - Cost : 0;
  };
  SmallVector<Candidate, 16> Candidates;
  for (unsigned V = 0, E = MRI.getNumVirtRegs(); V != E; ++V) {
    Register Reg = Register::index2VirtReg(V);
    if (!STI.getGPRClass()->hasSubClassEq(MRI.getRegClass(Reg)))
      continue;
    MachineInstr *Def = MRI.getUniqueVRegDef(Reg);
    if (!Def || Def->isPHI() || Def->isTerminator() || Def->isBundled() ||
        Def->getParent()->isEHPad())
      continue;
    Candidate C{Reg, Def, {}, {}, BitVector(), 0};
    bool Safe = true;
    for (MachineInstr &Use : MRI.use_instructions(Reg)) {
      if (Use.isDebugInstr()) {
        if (!Use.isDebugValue()) {
          Safe = false;
          break;
        }
        C.DebugUses.push_back(&Use);
        continue;
      }
      if (Use.isPHI() || Use.isBundled() || Use.isInlineAsm() ||
          Use.getParent()->isEHPad() || !MDT.dominates(Def, &Use)) {
        Safe = false;
        break;
      }
      if (!llvm::is_contained(C.Uses, &Use))
        C.Uses.push_back(&Use);
    }
    if (!Safe || C.Uses.empty())
      continue;
    llvm::sort(C.Uses, [&](const MachineInstr *A, const MachineInstr *B) {
      return Liveness.Position.lookup(A) < Liveness.Position.lookup(B);
    });
    C.Range = Liveness.range(*Def, C.Uses);
    C.Score = Score(C);
    if (C.Score)
      Candidates.push_back(std::move(C));
  }
  llvm::stable_sort(Candidates, [](const Candidate &A, const Candidate &B) {
    return A.Score > B.Score;
  });
  bool Changed = false;
  for (Candidate &C : Candidates) {
    if (!Score(C))
      continue;
    unsigned Choice = Occupied.size();
    // The spill allocator separately accounts for saving and restoring S5/S6.
    for (unsigned I = 0; I != Occupied.size(); ++I) {
      MCRegister Reg = RISCCSRegLiveness::Registers[I];
      if (Reg == RISCC::S5 || Reg == RISCC::S6)
        continue;
      if (!C.Range.anyCommon(Occupied[I])) {
        Choice = I;
        break;
      }
    }
    if (Choice == Occupied.size())
      continue;
    MCRegister SReg = RISCCSRegLiveness::Registers[Choice];
    LLVM_DEBUG(dbgs() << "ADDS placement in " << MF.getName() << ": score "
                      << Score(C) << ", S register " << TRI.getName(SReg)
                      << ", definition: " << *C.Def);
    Occupied[Choice] |= C.Range;
    BitVector Freed = FreedRange(C);
    BuildMI(*C.Def->getParent(), std::next(C.Def->getIterator()),
            C.Def->getDebugLoc(), TII.get(TargetOpcode::COPY), SReg)
        .addReg(C.Reg);
    DenseMap<MachineBasicBlock *, Register> Reloads;
    Reloads[C.Def->getParent()] = C.Reg;
    // Create virtual registers in instruction order, independent of pointer
    // hashing, so allocation and final code are reproducible across builds.
    for (MachineInstr *Use : C.Uses) {
      MachineBasicBlock *BB = Use->getParent();
      if (IsAddUse(*Use, C.Reg) || Reloads.contains(BB))
        continue;
      Register Temp = MRI.createVirtualRegister(MRI.getRegClass(C.Reg));
      BuildMI(*BB, Use, Use->getDebugLoc(), TII.get(TargetOpcode::COPY), Temp)
          .addReg(SReg);
      Reloads[BB] = Temp;
    }
    for (MachineInstr *Use : C.Uses) {
      if (IsAddUse(*Use, C.Reg)) {
        convertToAdds(*Use, C.Reg, SReg, TII);
      } else {
        for (MachineOperand &MO : Use->operands())
          if (MO.isReg() && MO.isUse() && MO.getReg() == C.Reg) {
            MO.setReg(Reloads.lookup(Use->getParent()));
            MO.setIsKill(false);
          }
      }
    }
    for (unsigned P : Freed.set_bits()) {
      if (!Liveness.isActive(C.Reg, *Liveness.Instructions[P / 2]->getParent()))
        continue;
      assert(Liveness.Pressure[P] && "selected value must be live");
      --Liveness.Pressure[P];
    }
    for (MachineInstr *Use : C.DebugUses)
      Use->setDebugValueUndef();
    // Preserve S liveness for successors and later post-allocation passes.
    for (MachineBasicBlock &MBB : MF) {
      if (&MBB == C.Def->getParent() || MBB.empty())
        continue;
      unsigned P = Liveness.blockBegin(MBB);
      if (C.Range.test(P) && !MBB.isLiveIn(SReg))
        MBB.addLiveIn(SReg);
    }
    Changed = true;
  }
  return Changed;
}
