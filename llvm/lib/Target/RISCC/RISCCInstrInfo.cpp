//===-- RISCCInstrInfo.cpp - RISCC Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCCInstrInfo.h"
#include "RISCC.h"
#include "RISCCMachineFunctionInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "RISCCGenInstrInfo.inc"

void RISCCInstrInfo::anchor() {}

RISCCInstrInfo::RISCCInstrInfo(const RISCCSubtarget &STI)
    : RISCCGenInstrInfo(STI, RI, RISCC::ADJCALLSTACKDOWN,
                        RISCC::ADJCALLSTACKUP),
      RI(STI), STI(STI) {}

void RISCCInstrInfo::materializeImmediate(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, const DebugLoc &DL,
    Register Destination, int64_t Value, MachineInstr::MIFlag Flag) const {
  if (STI.isRC32()) {
    if (!isUInt<8>(Value))
      report_fatal_error("RC32 immediate requires a literal-pool load");
    BuildMI(MBB, I, DL, get(RISCC::LDI32), Destination)
        .addImm(Value)
        .setMIFlag(Flag);
    return;
  }
  uint64_t Encoded = static_cast<uint16_t>(Value);
  if (isUInt<8>(Encoded)) {
    BuildMI(MBB, I, DL, get(RISCC::LDI), Destination)
        .addImm(Encoded)
        .setMIFlag(Flag);
    return;
  }
  if ((Encoded & 0xff) == 0) {
    BuildMI(MBB, I, DL, get(RISCC::LUI), Destination)
        .addImm(Encoded >> 8)
        .setMIFlag(Flag);
    return;
  }
  BuildMI(MBB, I, DL, get(RISCC::LDI16), Destination)
      .addImm(Value)
      .setMIFlag(Flag);
}

bool RISCCInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  if (MI.getOpcode() == RISCC::SEXT8_RC32 ||
      MI.getOpcode() == RISCC::SEXT16_RC32) {
    MachineBasicBlock &MBB = *MI.getParent();
    Register Dst = MI.getOperand(0).getReg();
    Register Src = MI.getOperand(1).getReg();
    const DebugLoc &DL = MI.getDebugLoc();
    if (MI.getOpcode() == RISCC::SEXT8_RC32) {
      BuildMI(MBB, MI, DL, get(RISCC::ANDI32), Dst)
          .addReg(Src).addImm(0xff);
      BuildMI(MBB, MI, DL, get(RISCC::XORI32), Dst)
          .addReg(Dst).addImm(0x80);
      BuildMI(MBB, MI, DL, get(RISCC::ADDI32), Dst)
          .addReg(Dst).addImm(-128);
    } else {
      MachineFunction *MF = MBB.getParent();
      MachineConstantPool *Pool = MF->getConstantPool();
      Type *I32 = Type::getInt32Ty(MF->getFunction().getContext());
      auto LoadR0 = [&](uint32_t Value) {
        const Constant *C = ConstantInt::get(I32, Value);
        unsigned CPI = Pool->getConstantPoolIndex(C, Align(4));
        BuildMI(MBB, MI, DL, get(RISCC::LDPC), RISCC::R0)
            .addConstantPoolIndex(CPI);
      };
      LoadR0(0xffff);
      BuildMI(MBB, MI, DL, get(RISCC::AND32), Dst)
          .addReg(Src).addReg(RISCC::R0);
      LoadR0(0x8000);
      BuildMI(MBB, MI, DL, get(RISCC::XOR32), Dst)
          .addReg(Dst).addReg(RISCC::R0);
      BuildMI(MBB, MI, DL, get(RISCC::SUB32), Dst)
          .addReg(Dst).addReg(RISCC::R0);
    }
    MI.eraseFromParent();
    return true;
  }
  if (MI.getOpcode() != RISCC::SEXT8_NANO)
    return false;

  MachineBasicBlock &MBB = *MI.getParent();
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  const DebugLoc &DL = MI.getDebugLoc();
  bool SourceIsByteLoad = false;
  for (auto I = MI.getIterator(); I != MBB.begin();) {
    --I;
    if (!I->modifiesRegister(Src, &RI))
      continue;
    SourceIsByteLoad =
        I->getOpcode() == RISCC::LDB && I->getOperand(0).getReg() == Src;
    break;
  }
  Register Extended = Src;
  if (!SourceIsByteLoad) {
    BuildMI(MBB, MI, DL, get(RISCC::ANDI), Dst).addReg(Src).addImm(0xff);
    Extended = Dst;
  }
  BuildMI(MBB, MI, DL, get(RISCC::XORI), Dst).addReg(Extended).addImm(0x80);
  BuildMI(MBB, MI, DL, get(RISCC::ADDI), Dst).addReg(Dst).addImm(-128);
  MI.eraseFromParent();
  return true;
}

void RISCCInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, Register Dst, Register Src,
                                 bool Kill, bool, bool) const {
  const TargetRegisterClass *GPR = STI.getGPRClass();
  const TargetRegisterClass *SReg = STI.getSRegClass();
  if (GPR->contains(Dst, Src)) {
    BuildMI(MBB, I, DL, get(STI.isRC32() ? RISCC::MOV32 : RISCC::MOV), Dst)
        .addReg(Src, getKillRegState(Kill));
    return;
  }
  if (GPR->contains(Dst) && SReg->contains(Src)) {
    BuildMI(MBB, I, DL, get(RISCC::MFS), Dst)
        .addReg(Src, getKillRegState(Kill));
    return;
  }
  if (SReg->contains(Dst) && GPR->contains(Src)) {
    BuildMI(MBB, I, DL, get(RISCC::MTS), Dst)
        .addReg(Src, getKillRegState(Kill));
    return;
  }
  llvm_unreachable("unsupported RISC-C physical-register copy");
}

void RISCCInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register Src,
    bool Kill, int FI, const TargetRegisterClass *RC, Register,
    MachineInstr::MIFlag Flags) const {
  const bool GPR = STI.getGPRClass()->hasSubClassEq(RC);
  const bool SReg = STI.getSRegClass()->hasSubClassEq(RC);
  assert((GPR || SReg) &&
         "only GPR and S-register spills are supported");
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOStore,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));
  if (SReg) {
    BuildMI(MBB, I, DebugLoc(), get(RISCC::MFS), RISCC::R0)
        .addReg(Src, getKillRegState(Kill));
    BuildMI(MBB, I, DebugLoc(), get(STI.isRC32() ? RISCC::ST32 : RISCC::ST))
        .addReg(RISCC::R0).addFrameIndex(FI).addImm(0)
        .addMemOperand(MMO).setMIFlag(Flags);
    return;
  }
  BuildMI(MBB, I, DebugLoc(),
          get(STI.isRC32() ? RISCC::ST32
                           : STI.isNano() ? RISCC::ST_NANO : RISCC::ST))
      .addReg(Src, getKillRegState(Kill)).addFrameIndex(FI).addImm(0)
      .addMemOperand(MMO).setMIFlag(Flags);
}

void RISCCInstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register Dst,
    int FI, const TargetRegisterClass *RC, Register, unsigned,
    MachineInstr::MIFlag Flags) const {
  const bool GPR = STI.getGPRClass()->hasSubClassEq(RC);
  const bool SReg = STI.getSRegClass()->hasSubClassEq(RC);
  assert((GPR || SReg) &&
         "only GPR and S-register reloads are supported");
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOLoad,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));
  if (SReg) {
    BuildMI(MBB, I, DebugLoc(), get(STI.isRC32() ? RISCC::LD32 : RISCC::LD),
            RISCC::R0)
        .addFrameIndex(FI).addImm(0).addMemOperand(MMO).setMIFlag(Flags);
    BuildMI(MBB, I, DebugLoc(), get(RISCC::MTS), Dst).addReg(RISCC::R0);
    return;
  }
  BuildMI(MBB, I, DebugLoc(),
          get(STI.isRC32() ? RISCC::LD32
                           : STI.isNano() ? RISCC::LD_NANO : RISCC::LD), Dst)
      .addFrameIndex(FI).addImm(0).addMemOperand(MMO).setMIFlag(Flags);
}

bool RISCCInstrInfo::isConditionalBranchOpcode(unsigned Opcode) {
  return Opcode == RISCC::BEQZ || Opcode == RISCC::BNEZ ||
         Opcode == RISCC::BLTZ || Opcode == RISCC::BGEZ;
}

unsigned RISCCInstrInfo::getOppositeBranchOpcode(unsigned Opcode) {
  switch (Opcode) {
  case RISCC::BEQZ:
    return RISCC::BNEZ;
  case RISCC::BNEZ:
    return RISCC::BEQZ;
  case RISCC::BLTZ:
    return RISCC::BGEZ;
  case RISCC::BGEZ:
    return RISCC::BLTZ;
  default:
    llvm_unreachable("unexpected RISC-C conditional branch");
  }
}

bool RISCCInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && Cond[0].isImm());
  if (!isConditionalBranchOpcode(Cond[0].getImm()))
    return true;
  Cond[0].setImm(getOppositeBranchOpcode(Cond[0].getImm()));
  return false;
}

bool RISCCInstrInfo::analyzeBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *&TBB, MachineBasicBlock *&FBB,
    SmallVectorImpl<MachineOperand> &Cond, bool) const {
  auto I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return false;
  if (I->getOpcode() == RISCC::JMP8 || I->getOpcode() == RISCC::JMP16) {
    if (!I->getOperand(0).isMBB())
      return true;
    TBB = I->getOperand(0).getMBB();
    if (I == MBB.begin())
      return false;
    --I;
    while (I->isDebugInstr() && I != MBB.begin())
      --I;
    if (isConditionalBranchOpcode(I->getOpcode()) &&
        I->getOperand(0).isMBB()) {
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(I->getOpcode()));
    }
    return false;
  }
  if (isConditionalBranchOpcode(I->getOpcode()) &&
      I->getOperand(0).isMBB()) {
    TBB = I->getOperand(0).getMBB();
    Cond.push_back(MachineOperand::CreateImm(I->getOpcode()));
    return false;
  }
  return I->isTerminator();
}

unsigned RISCCInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  unsigned Count = 0, Bytes = 0;
  while (!MBB.empty()) {
    auto I = MBB.getLastNonDebugInstr();
    if (I == MBB.end() || (!isConditionalBranchOpcode(I->getOpcode()) &&
                           I->getOpcode() != RISCC::JMP8 &&
                           I->getOpcode() != RISCC::JMP16))
      break;
    Bytes += getInstSizeInBytes(*I);
    I->eraseFromParent();
    ++Count;
  }
  if (BytesRemoved)
    *BytesRemoved = Bytes;
  return Count;
}

unsigned RISCCInstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  assert(TBB && Cond.size() <= 1);
  unsigned Count = 0, Bytes = 0;
  if (!Cond.empty()) {
    BuildMI(&MBB, DL, get(Cond[0].getImm())).addMBB(TBB);
    ++Count;
    Bytes += 2;
    if (FBB) {
      BuildMI(&MBB, DL, get(RISCC::JMP8)).addMBB(FBB);
      ++Count;
      Bytes += 2;
    }
  } else {
    BuildMI(&MBB, DL, get(RISCC::JMP8)).addMBB(TBB);
    ++Count;
    Bytes += 2;
  }
  if (BytesAdded)
    *BytesAdded = Bytes;
  return Count;
}

bool RISCCInstrInfo::isBranchOffsetInRange(unsigned Opcode,
                                           int64_t BrOffset) const {
  if (Opcode == RISCC::JMP16)
    return true;
  if (Opcode != RISCC::JMP8 && !isConditionalBranchOpcode(Opcode))
    return true;
  // RC32 literal islands are emitted by the AsmPrinter after branch
  // relaxation. Their bytes are intentionally absent from MachineInstr
  // offsets, so a nominally short branch may span considerably more than the
  // relaxation pass can see. Route RC32 branches through the existing
  // LDPC/JALR long-branch sequence; the inverse conditional used to skip that
  // sequence remains an adjacent short branch.
  if (STI.isRC32() && std::abs(BrOffset) > 32)
    return false;
  // Short branches encode a signed word displacement from the following
  // instruction.
  int64_t Displacement = BrOffset - 2;
  return (Displacement & 1) == 0 && isInt<8>(Displacement / 2);
}

MachineBasicBlock *
RISCCInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();
  if (Opcode != RISCC::JMP8 && Opcode != RISCC::JMP16 &&
      !isConditionalBranchOpcode(Opcode))
    return nullptr;
  return MI.getOperand(0).isMBB() ? MI.getOperand(0).getMBB() : nullptr;
}

void RISCCInstrInfo::insertIndirectBranch(
    MachineBasicBlock &MBB, MachineBasicBlock &DestBB,
    MachineBasicBlock &RestoreBB, const DebugLoc &DL, int64_t,
    RegScavenger *RS) const {
  assert(MBB.empty() && MBB.pred_size() == 1 &&
         "expected a fresh long-branch block");
  assert(RestoreBB.empty() && "expected an empty restore block");
  if (STI.hasLongJall() && !STI.isRC32()) {
    BuildMI(MBB, MBB.end(), DL, get(RISCC::JMP16)).addMBB(&DestBB);
    return;
  }

  assert(RS && "register scavenger required for a short-call-profile branch");
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  Register VirtualScratch = MRI.createVirtualRegister(STI.getGPRClass());
  MachineInstr &Address =
      *BuildMI(MBB, MBB.end(), DL, get(RISCC::LDI16), VirtualScratch)
           .addMBB(&DestBB);
  if (STI.isNano())
    BuildMI(MBB, MBB.end(), DL, get(RISCC::JALR_NANO), RISCC::R0)
        .addReg(VirtualScratch, RegState::Kill);
  else
    BuildMI(MBB, MBB.end(), DL, get(RISCC::JALR), RISCC::S0)
        .addReg(VirtualScratch, RegState::Kill);

  RS->enterBasicBlockEnd(MBB);
  Register Scratch = RS->scavengeRegisterBackwards(
      *STI.getGPRClass(), Address.getIterator(), /*RestoreAfter=*/false,
      /*SPAdj=*/0, /*AllowSpill=*/false);
  if (Scratch) {
    RS->setRegUsed(Scratch);
  } else {
    Scratch = RISCC::R0;
    int FI = MF.getInfo<RISCCMachineFunctionInfo>()
                 ->getBranchRelaxationSpillFI();
    if (FI < 0)
      report_fatal_error("RISC-C function size was underestimated");

    storeRegToStackSlot(MBB, Address.getIterator(), Scratch, true, FI,
                        STI.getGPRClass(), Register(),
                        MachineInstr::NoFlags);
    STI.getRegisterInfo()->eliminateFrameIndex(
        std::prev(Address.getIterator()), 0, 1, RS);

    Address.getOperand(1).setMBB(&RestoreBB);
    loadRegFromStackSlot(RestoreBB, RestoreBB.end(), Scratch, FI,
                         STI.getGPRClass(), Register(), 0,
                         MachineInstr::NoFlags);
    STI.getRegisterInfo()->eliminateFrameIndex(RestoreBB.back(), 0, 1, RS);
  }

  MRI.replaceRegWith(VirtualScratch, Scratch);
  MRI.clearVirtRegs();
}

unsigned RISCCInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case TargetOpcode::CFI_INSTRUCTION:
  case TargetOpcode::EH_LABEL:
  case TargetOpcode::IMPLICIT_DEF:
  case TargetOpcode::KILL:
  case TargetOpcode::DBG_VALUE:
    return 0;
  case TargetOpcode::INLINEASM:
  case TargetOpcode::INLINEASM_BR:
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              MI.getMF()->getTarget().getMCAsmInfo());
  case TargetOpcode::BUNDLE:
    return getInstBundleSize(MI);
  default:
    return MI.getDesc().getSize();
  }
}
