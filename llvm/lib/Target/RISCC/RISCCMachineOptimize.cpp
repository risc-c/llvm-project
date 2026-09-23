//===-- RISCCMachineOptimize.cpp - Machine SSA optimizations --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Clean up machine SSA before register allocation: preserve narrow-value
// facts across blocks and avoid keeping cheap derived values across calls.
//===----------------------------------------------------------------------===//
#include "RISCCMachineOptimize.h"
#include "RISCCInstrInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/InitializePasses.h"

using namespace llvm;
#define DEBUG_TYPE "riscc-machine-optimize"

namespace {
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
      bool Local = true;
      for (MachineInstr &Use : MRI.use_nodbg_instructions(Dst)) {
        if (Use.getParent() != &MBB || Use.isPHI()) {
          Local = false;
          break;
        }
      }
      if (!Local)
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

static std::optional<APInt> getConstant(const MachineFunction &MF, Register Reg,
                                        unsigned FullWidth) {
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
  auto Width = [&](Register Reg) {
    auto I = Widths.find(Reg);
    return I == Widths.end() ? FullWidth : I->second;
  };
  auto UnsignedWidth = [&](Register Reg) {
    auto I = UnsignedWidths.find(Reg);
    return I == UnsignedWidths.end() ? FullWidth : I->second;
  };
  // Start with unknown values. Each iteration only narrows widths, so cycles
  // without a known defining value remain conservatively unknown.
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
          return Width(MI.getOperand(I).getReg());
        };
        auto ImmWidth = [&](unsigned I) {
          return APInt(64, MI.getOperand(I).getImm())
              .trunc(FullWidth)
              .getSignificantBits();
        };
        auto SrcUnsignedWidth = [&](unsigned I) {
          return UnsignedWidth(MI.getOperand(I).getReg());
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
          if (auto C = getConstant(MF, MI.getOperand(0).getReg(), FullWidth)) {
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
              if (auto C =
                      getConstant(MF, MI.getOperand(I).getReg(), FullWidth);
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
          if (!MI.getOperand(1).getSubReg()) {
            Bits = Width(MI.getOperand(1).getReg());
            UnsignedBits = SrcUnsignedWidth(1);
          }
          break;
        case TargetOpcode::PHI:
          Bits = 1;
          UnsignedBits = 0;
          for (unsigned I = 1; I < MI.getNumOperands(); I += 2) {
            Bits = std::max(Bits, Width(MI.getOperand(I).getReg()));
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
      unsigned Bits = getExtensionWidth(MI.getOpcode());
      bool Redundant = Bits && Width(MI.getOperand(1).getReg()) <= Bits;
      if (MI.getOpcode() == RISCC::ANDI || MI.getOpcode() == RISCC::ANDI32) {
        APInt Mask(FullWidth, MI.getOperand(2).getImm());
        Redundant = Mask.isMask() && UnsignedWidth(MI.getOperand(1).getReg()) <=
                                         Mask.countTrailingOnes();
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
    return delayAddsAcrossCalls(MF) || Changed;
  }
};
} // namespace
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
  Changed |= delayAddsAcrossCalls(MF);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
