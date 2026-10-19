//===-- RISCCCopyOptimize.cpp - Local physical value placement ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Treat copies as value aliases in a bounded, straight-line region. Assign
// each computation a register without overwriting a still-needed value, then
// restore the region's live-out assignments. This handles copy chains and
// copy cycles without prescribing an opcode sequence or a memory-access shape.
// Non-copy instructions and their memory operations stay in their original
// order. Planning is separate from mutation: an unsuccessful plan changes
// nothing, and a successful plan must remove at least one instruction.
//===----------------------------------------------------------------------===//

#include "RISCCInstrInfo.h"
#include "RISCCMachineOptimize.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/IR/Function.h"
#include <array>
#include <numeric>

using namespace llvm;

namespace {

constexpr unsigned NumGPRs = 8;
constexpr unsigned NoValue = ~0u;
constexpr unsigned MaxRegionInstructions = 16;

unsigned regIndex(Register Reg) { return Reg - RISCC::R0; }
Register regAt(unsigned Index) { return RISCC::R0 + Index; }

// A null Original denotes an inserted copy, with destination/source in Regs.
struct PlannedInstruction {
  MachineInstr *Original;
  SmallVector<Register, 4> Regs;
};

class LocalCopyPlanner {
  const RISCCSubtarget &STI;
  const RISCCInstrInfo &TII;
  const RISCCRegisterInfo &TRI;
  TargetSchedModel Schedule;
  unsigned MoveOp;

  bool canUse(const MachineInstr &MI, unsigned Op, Register Reg) const {
    const MachineOperand &MO = MI.getOperand(Op);
    if (MO.getReg() == Reg)
      return true;
    if (!MO.isRenamable())
      return false;
    const auto *RC = MI.getRegClassConstraint(Op, &TII, &TRI);
    return RC && RC->contains(Reg);
  }

  bool isCopy(const MachineInstr &MI) const {
    return MI.getOpcode() == MoveOp && MI.getNumOperands() == 2;
  }

  // In-order issue cost, including producer latency and live-out readiness.
  // Moving copies must not turn a useful load-latency gap into a speed loss.
  std::array<unsigned, NumGPRs + 1>
  completionTimes(ArrayRef<PlannedInstruction> Plan) const {
    std::array<unsigned, NumGPRs + 1> Ready{};
    unsigned &Issue = Ready.back();
    for (const auto &Step : Plan) {
      if (!Step.Original) {
        Issue = std::max(Issue, Ready[regIndex(Step.Regs[1])]);
        Ready[regIndex(Step.Regs[0])] = ++Issue;
        continue;
      }
      const MachineInstr &MI = *Step.Original;
      for (unsigned Op = 0; Op != MI.getNumOperands(); ++Op)
        if (MI.getOperand(Op).isReg() && MI.getOperand(Op).isUse())
          Issue = std::max(Issue, Ready[regIndex(Step.Regs[Op])]);
      if (MI.getNumDefs())
        Ready[regIndex(Step.Regs[0])] =
            Issue + Schedule.computeInstrLatency(&MI);
      Issue += std::max(1u, Schedule.getNumMicroOps(&MI));
    }
    for (unsigned R = 0; R != NumGPRs; ++R)
      Ready[R] = std::max(Ready[R], Issue);
    return Ready;
  }

public:
  explicit LocalCopyPlanner(MachineFunction &MF)
      : STI(MF.getSubtarget<RISCCSubtarget>()), TII(*STI.getInstrInfo()),
        TRI(*STI.getRegisterInfo()), MoveOp(TII.getNativeOpcode(RISCC::MOV)) {
    Schedule.init(&STI);
  }

  bool startsRegion(const MachineInstr &MI) const {
    return isEligible(MI) && isCopy(MI);
  }

  bool isEligible(const MachineInstr &MI) const {
    // MOV is an MC encoding alias with ordinary GPR operands. Other pseudos
    // can expand to several instructions with additional scratch constraints.
    if (MI.isMetaInstruction() || MI.isPosition() ||
        (MI.isPseudo() && MI.getOpcode() != MoveOp) || MI.isCall() ||
        MI.isTerminator() || MI.isInlineAsm() || MI.isBundled() ||
        MI.isBundle() || MI.hasUnmodeledSideEffects() ||
        MI.hasOrderedMemoryRef() || MI.getPreInstrSymbol() ||
        MI.getPostInstrSymbol() || MI.peekDebugInstrNum() ||
        MI.getFlag(MachineInstr::FrameSetup) ||
        MI.getFlag(MachineInstr::FrameDestroy) || MI.getNumDefs() > 1 ||
        MI.getNumOperands() != MI.getNumExplicitOperands() ||
        MI.getDesc().hasExtraSrcRegAllocReq() ||
        MI.getDesc().hasExtraDefRegAllocReq())
      return false;
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isRegMask())
        return false;
      if (!MO.isReg())
        continue;
      if (!STI.getGPRClass()->contains(MO.getReg()) || MO.isImplicit() ||
          MO.isUndef() || MO.getSubReg() || MO.isEarlyClobber() ||
          MO.isInternalRead() || (MO.isDef() && MO.getReg() == RISCC::R7))
        return false;
    }
    return true;
  }

  bool plan(ArrayRef<MachineInstr *> Region, unsigned LiveOut, bool ForSize,
            SmallVectorImpl<PlannedInstruction> &Plan) const {
    struct ValueInstruction {
      SmallVector<unsigned, 4> Uses;
      unsigned Def = NoValue;
      bool Copy;
    };
    SmallVector<ValueInstruction, MaxRegionInstructions> Values;
    std::array<unsigned, NumGPRs> Logical, Actual;
    std::iota(Logical.begin(), Logical.end(), 0);
    Actual = Logical;
    SmallVector<unsigned, NumGPRs + MaxRegionInstructions> LastUse(NumGPRs, 0);
    unsigned Writable = 0;

    // Value numbers distinguish each real definition; copies only add aliases.
    for (unsigned Index = 0; Index != Region.size(); ++Index) {
      MachineInstr &MI = *Region[Index];
      ValueInstruction VI;
      VI.Copy = isCopy(MI);
      VI.Uses.resize(MI.getNumOperands(), NoValue);
      for (unsigned Op = 0; Op != MI.getNumOperands(); ++Op) {
        const MachineOperand &MO = MI.getOperand(Op);
        if (MO.isReg() && MO.isUse()) {
          unsigned Value = Logical[regIndex(MO.getReg())];
          VI.Uses[Op] = Value;
          if (!VI.Copy || !MO.isRenamable())
            LastUse[Value] = Index + 1;
        }
      }
      if (MI.getNumDefs()) {
        unsigned Dst = regIndex(MI.getOperand(0).getReg());
        Writable |= 1u << Dst;
        if (!MI.getOperand(0).isRenamable())
          LiveOut |= 1u << Dst;
        if (VI.Copy)
          Logical[Dst] = VI.Uses[1];
        else {
          VI.Def = LastUse.size();
          LastUse.push_back(0);
          Logical[Dst] = VI.Def;
        }
      }
      Values.push_back(std::move(VI));
    }
    for (unsigned R = 0; R != NumGPRs; ++R)
      if (LiveOut & (1u << R))
        LastUse[Logical[R]] = Region.size() + 1;

    auto CanOverwrite = [&](unsigned R, unsigned Position) {
      return (Writable & (1u << R)) && (LastUse[Actual[R]] <= Position ||
                                        llvm::count(Actual, Actual[R]) > 1);
    };
    auto AddCopy = [&](unsigned Dst, unsigned Src) {
      Plan.push_back({nullptr, {regAt(Dst), regAt(Src)}});
      Actual[Dst] = Actual[Src];
    };

    for (unsigned Index = 0; Index != Region.size(); ++Index) {
      MachineInstr &MI = *Region[Index];
      const auto &VI = Values[Index];
      if (VI.Copy) {
        // Fixed ABI copies still describe aliases, but their required
        // physical source/destination must hold that value at this point.
        // A prior copy may already have established the destination directly.
        unsigned Src = regIndex(MI.getOperand(1).getReg());
        unsigned Dst = regIndex(MI.getOperand(0).getReg());
        unsigned Value = VI.Uses[1];
        if (!MI.getOperand(1).isRenamable() && Actual[Src] != Value)
          return false;
        if (!MI.getOperand(0).isRenamable() && Actual[Dst] != Value) {
          if (!CanOverwrite(Dst, Index + 1))
            return false;
          auto Source = llvm::find(Actual, Value);
          if (Source == Actual.end())
            return false;
          AddCopy(Dst, Source - Actual.begin());
        }
        // Place a value directly in a dead final home when possible. This
        // preserves it across later overwrites without a temporary copy chain.
        for (unsigned R = 0; R != NumGPRs; ++R)
          if ((LiveOut & (1u << R)) && Logical[R] == VI.Uses[1] &&
              Actual[R] != Logical[R] && CanOverwrite(R, Index + 1)) {
            auto Src = llvm::find(Actual, Logical[R]);
            if (Src == Actual.end())
              return false;
            AddCopy(R, Src - Actual.begin());
          }
        continue;
      }

      PlannedInstruction Step{&MI, {}};
      Step.Regs.resize(MI.getNumOperands());
      for (unsigned Op = 0; Op != MI.getNumOperands(); ++Op) {
        if (VI.Uses[Op] == NoValue)
          continue;
        unsigned Preferred = regIndex(MI.getOperand(Op).getReg());
        for (unsigned N = 0; N != NumGPRs; ++N) {
          unsigned R = (Preferred + N) % NumGPRs;
          if (Actual[R] == VI.Uses[Op] && canUse(MI, Op, regAt(R))) {
            Step.Regs[Op] = regAt(R);
            break;
          }
        }
        if (!Step.Regs[Op])
          return false;
      }
      if (VI.Def != NoValue) {
        unsigned Preferred = regIndex(MI.getOperand(0).getReg());
        unsigned TiedUse = 0;
        bool Tied = MI.isRegTiedToUseOperand(0, &TiedUse);
        for (unsigned N = 0; N != NumGPRs; ++N) {
          unsigned R = (Preferred + N) % NumGPRs;
          if (!CanOverwrite(R, Index + 1) || !canUse(MI, 0, regAt(R)) ||
              (Tied && (Actual[R] != VI.Uses[TiedUse] ||
                        !canUse(MI, TiedUse, regAt(R)))))
            continue;
          Step.Regs[0] = regAt(R);
          if (Tied)
            Step.Regs[TiedUse] = regAt(R);
          Actual[R] = VI.Def;
          break;
        }
        if (!Step.Regs[0])
          return false;
      }
      Plan.push_back(std::move(Step));
    }

    // Resolve the remaining live-out assignments as parallel copies. A cycle
    // that needs an additional scratch register is conservatively left alone.
    unsigned Pending = LiveOut;
    while (Pending) {
      bool Progress = false;
      for (unsigned R = 0; R != NumGPRs; ++R) {
        if (!(Pending & (1u << R)))
          continue;
        if (Actual[R] != Logical[R]) {
          if (!(Writable & (1u << R)))
            return false;
          bool Needed = false;
          for (unsigned Other = 0; Other != NumGPRs; ++Other)
            Needed |= (Pending & (1u << Other)) && Logical[Other] == Actual[R];
          if (Needed && llvm::count(Actual, Actual[R]) == 1)
            continue;
          auto Src = llvm::find(Actual, Logical[R]);
          if (Src == Actual.end())
            return false;
          AddCopy(R, Src - Actual.begin());
        }
        Pending &= ~(1u << R);
        Progress = true;
      }
      if (!Progress)
        return false;
    }
    if (Plan.size() >= Region.size())
      return false;
    if (!ForSize) {
      SmallVector<PlannedInstruction, MaxRegionInstructions> Original;
      for (MachineInstr *MI : Region) {
        PlannedInstruction Step{MI, {}};
        for (const MachineOperand &MO : MI->operands())
          Step.Regs.push_back(MO.isReg() ? MO.getReg() : Register());
        Original.push_back(std::move(Step));
      }
      auto Before = completionTimes(Original), After = completionTimes(Plan);
      if (After.back() > Before.back())
        return false;
      for (unsigned R = 0; R != NumGPRs; ++R)
        if ((LiveOut & (1u << R)) && After[R] > Before[R])
          return false;
    }
    return true;
  }

  void apply(ArrayRef<MachineInstr *> Region,
             ArrayRef<PlannedInstruction> Plan) const {
    MachineBasicBlock &MBB = *Region.front()->getParent();
    MachineFunction &MF = *MBB.getParent();
    auto Insert = Region.front()->getIterator();
    for (const auto &Step : Plan) {
      if (!Step.Original) {
        BuildMI(MBB, Insert, Region.front()->getDebugLoc(), TII.get(MoveOp),
                Step.Regs[0])
            .addReg(Step.Regs[1]);
        continue;
      }
      MachineInstr *New = MF.CloneMachineInstr(Step.Original);
      for (unsigned Op = 0; Op != New->getNumOperands(); ++Op)
        if (New->getOperand(Op).isReg())
          New->getOperand(Op).setReg(Step.Regs[Op]);
      MBB.insert(Insert, New);
    }
    for (MachineInstr *MI : Region)
      MI->eraseFromParent();
  }
};

} // namespace

bool llvm::coalesceRISCCLocalCopies(MachineFunction &MF) {
  if (!MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::TracksLiveness))
    return false;
  const auto &TRI = *MF.getSubtarget<RISCCSubtarget>().getRegisterInfo();
  LocalCopyPlanner Planner(MF);
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    DenseMap<const MachineInstr *, unsigned> LiveAfter;
    LivePhysRegs Live(TRI);
    Live.addLiveOuts(MBB);
    for (const MachineInstr &MI : llvm::reverse(MBB)) {
      unsigned Mask = 0;
      for (unsigned R = 0; R != NumGPRs; ++R)
        if (Live.contains(regAt(R)))
          Mask |= 1u << R;
      LiveAfter[&MI] = Mask;
      Live.stepBackward(MI);
      // Debug operands are not execution uses, but their named values must
      // still be available when a location description is encountered.
      if (MI.isDebugInstr())
        for (const MachineOperand &MO : MI.operands())
          if (MO.isReg() && MO.getReg().isPhysical() &&
              RISCC::GPRRegClass.contains(MO.getReg()))
            Live.addReg(MO.getReg());
    }
    bool BlockChanged = false;
    for (auto I = MBB.begin(); I != MBB.end();) {
      if (!Planner.startsRegion(*I++))
        continue;
      SmallVector<MachineInstr *, MaxRegionInstructions> Region{&*std::prev(I)};
      for (auto J = I; J != MBB.end() && Planner.isEligible(*J) &&
                       Region.size() < MaxRegionInstructions;
           ++J)
        Region.push_back(&*J);
      // Try longest regions first, but let a later constraint end a profitable
      // prefix instead of invalidating all earlier opportunities.
      while (Region.size() > 1) {
        SmallVector<PlannedInstruction, MaxRegionInstructions> Plan;
        if (Planner.plan(Region, LiveAfter.lookup(Region.back()),
                         MF.getFunction().hasOptSize(), Plan)) {
          I = std::next(Region.back()->getIterator());
          Planner.apply(Region, Plan);
          BlockChanged = true;
          break;
        }
        Region.pop_back();
      }
    }
    if (BlockChanged) {
      recomputeLivenessFlags(MBB);
      Changed = true;
    }
  }
  return Changed;
}
