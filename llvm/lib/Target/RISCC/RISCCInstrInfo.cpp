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
#include "RISCCMatInt.h"
#include "RISCCSubtarget.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

static cl::opt<bool> EnableSizeLayout(
    "riscc-size-layout", cl::Hidden, cl::init(true),
    cl::desc("Refine size-optimized RISC-C block layout when it saves bytes"));
static cl::opt<bool> EnableSelectiveRemat(
    "riscc-selective-remat", cl::Hidden, cl::init(true),
    cl::desc("Reconstruct cheap RISC-C values with local uses across calls"));

#define GET_INSTRINFO_CTOR_DTOR
#define GET_INSTRMAP_INFO
#include "RISCCGenInstrInfo.inc"

void RISCCInstrInfo::anchor() {}

RISCCInstrInfo::RISCCInstrInfo(const RISCCSubtarget &STI)
    : RISCCGenInstrInfo(STI, RI, RISCC::ADJCALLSTACKDOWN,
                        RISCC::ADJCALLSTACKUP),
      RI(STI), STI(STI) {}

unsigned RISCCInstrInfo::getNativeOpcode(unsigned RC16Opcode) const {
  if (!STI.isRC32())
    return RC16Opcode;
  int32_t Opcode = RISCC::getRC32Opcode(RC16Opcode);
  assert(Opcode >= 0 && "instruction has no native-width pair");
  return Opcode;
}

unsigned RISCCInstrInfo::getTailMergeSize(const MachineFunction &MF) const {
  // Two compact instructions can repay a compact branch. Keep the generic
  // threshold for speed builds, where sharing may add a frequently taken edge.
  return MF.getFunction().hasOptSize() ? 2
                                       : TargetInstrInfo::getTailMergeSize(MF);
}

bool RISCCInstrInfo::enableSizeAwareBlockPlacement(
    const MachineFunction &MF) const {
  return EnableSizeLayout && MF.getFunction().hasOptSize();
}

bool RISCCInstrInfo::isReMaterializableImpl(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();
  if (Opcode != RISCC::LDI16 && Opcode != RISCC::FRAMEADDR &&
      Opcode != RISCC::FRAMEADDR32)
    return TargetInstrInfo::isReMaterializableImpl(MI);
  if (!EnableSelectiveRemat || !TargetInstrInfo::isReMaterializableImpl(MI))
    return false;
  const MachineFunction &MF = *MI.getMF();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  Register Dst = MI.getOperand(0).getReg();
  if (MF.getFunction().hasOptNone() || !Dst.isVirtual())
    return false;
  if (Opcode != RISCC::LDI16) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    if (!MI.getOperand(1).isFI() || !MI.getOperand(2).isImm() ||
        MFI.hasVarSizedObjects() || MFI.getMaxAlign() > STI.getStackAlignment())
      return false;
    int FI = MI.getOperand(1).getIndex();
    int64_t Offset = MI.getOperand(2).getImm();
    if (FI < 0)
      Offset += MFI.getObjectOffset(FI);
    // Estimate MOV plus one ADDI, allowing for GPR/S-register saves, the
    // return link and scavenger slots. Later spills can exceed this budget;
    // final frame-index lowering still handles a larger address correctly.
    uint64_t FrameBound = MFI.estimateStackSize(MF) + 16 * STI.getSlotSize();
    if (Offset < 0 || Offset > 127 || FrameBound > uint64_t(127 - Offset))
      return false;
  }
  // A single same-block use has the definition's execution frequency. Moving
  // its reconstruction past a call never duplicates a two-instruction value
  // into a hot inner loop. Cheap S-register caching wins for repeated uses.
  SmallVector<const MachineInstr *, 1> Uses;
  for (const MachineInstr &Use : MRI.use_nodbg_instructions(Dst)) {
    if (Use.getParent() != MI.getParent() || !Uses.empty())
      return false;
    Uses.push_back(&Use);
  }
  bool CrossedCall = false;
  for (auto I = std::next(MI.getIterator()); I != MI.getParent()->end(); ++I) {
    if (CrossedCall && llvm::is_contained(Uses, &*I))
      return true;
    CrossedCall |= I->isCall() && !I->isReturn();
  }
  return false;
}

bool RISCCInstrInfo::isFunctionSafeToOutlineFrom(
    MachineFunction &MF, bool OutlineFromLinkOnceODRs) const {
  const Function &F = MF.getFunction();
  // Outlined helpers use the existing private S3 link and no frame. IPRA runs
  // before outlining and would otherwise publish masks missing this clobber.
  return !STI.isNano() && F.hasOptSize() && !F.hasOptNone() &&
         !MF.getTarget().Options.EnableIPRA && !F.hasSection() &&
         !F.hasFnAttribute("interrupt") && !F.hasPersonalityFn() &&
         !F.hasFnAttribute(Attribute::UWTable) &&
         (OutlineFromLinkOnceODRs || !F.hasLinkOnceODRLinkage());
}

bool RISCCInstrInfo::shouldOutlineFromFunctionByDefault(
    MachineFunction &MF) const {
  return MF.getFunction().hasOptSize();
}

bool RISCCInstrInfo::isMBBSafeToOutlineFrom(MachineBasicBlock &MBB,
                                            unsigned &Flags) const {
  if (!TargetInstrInfo::isMBBSafeToOutlineFrom(MBB, Flags))
    return false;
  // Calls in tight loops can cost more than the code saving is worth. Leave
  // loop bodies inline even for size builds.
  MachineDominatorTree MDT(*MBB.getParent());
  MachineLoopInfo MLI(MDT);
  return !MLI.getLoopFor(&MBB);
}

outliner::InstrType RISCCInstrInfo::getOutliningTypeImpl(
    const MachineModuleInfo &, MachineBasicBlock::iterator &I, unsigned) const {
  const MachineInstr &MI = *I;
  if (MI.isCall() || MI.isTerminator() || MI.isCFIInstruction() ||
      MI.hasUnmodeledSideEffects() || MI.isBundled() || MI.isNotDuplicable() ||
      MI.getFlag(MachineInstr::FrameSetup) ||
      MI.getFlag(MachineInstr::FrameDestroy) || MI.getOpcode() == RISCC::LDPC ||
      MI.modifiesRegister(RISCC::R7, &RI))
    return outliner::InstrType::Illegal;
  // Literal calls use R0 as their target and S3 as the link. Neither may be
  // part of the shared sequence; liveness across it is checked separately.
  for (MCRegister Reg : {RISCC::R0, RISCC::S3})
    if (MI.readsRegister(Reg, &RI) || MI.modifiesRegister(Reg, &RI))
      return outliner::InstrType::Illegal;
  return outliner::InstrType::Legal;
}

std::optional<std::unique_ptr<outliner::OutlinedFunction>>
RISCCInstrInfo::getOutliningCandidateInfo(
    const MachineModuleInfo &, std::vector<outliner::Candidate> &Candidates,
    unsigned MinRepeats) const {
  llvm::erase_if(Candidates, [&](auto &C) {
    return C.isAnyUnavailableAcrossOrOutOfSeq({RISCC::R0, RISCC::S3}, RI);
  });
  if (Candidates.size() < MinRepeats)
    return std::nullopt;
  for (auto &C : Candidates) {
    const auto &ST = C.getMF()->getSubtarget<RISCCSubtarget>();
    // RC32 pays for LDPC/JALR, a word, alignment and a dedicated skip. Sharing
    // a literal or linker relaxation may improve this conservative bound.
    unsigned Cost = ST.isRC32() ? 12 : (ST.hasLongJall() ? 4 : 6);
    C.setCallInfo(0, Cost);
  }
  unsigned Bytes = 0;
  for (const MachineInstr &MI : Candidates.front())
    Bytes += getInstSizeInBytes(MI);
  // RET plus potential alignment at the new function boundary.
  return std::make_unique<outliner::OutlinedFunction>(Candidates, Bytes, 4, 0);
}

void RISCCInstrInfo::buildOutlinedFrame(
    MachineBasicBlock &MBB, MachineFunction &MF,
    const outliner::OutlinedFunction &) const {
  MF.getInfo<RISCCMachineFunctionInfo>()->setReturnAddressReg(RISCC::S3);
  MBB.addLiveIn(RISCC::S3);
  BuildMI(MBB, MBB.end(), DebugLoc(), get(RISCC::RET)).addReg(RISCC::S3);
}

MachineBasicBlock::iterator RISCCInstrInfo::insertOutlinedCall(
    Module &M, MachineBasicBlock &MBB, MachineBasicBlock::iterator &I,
    MachineFunction &MF, outliner::Candidate &) const {
  const auto &ST = MBB.getParent()->getSubtarget<RISCCSubtarget>();
  unsigned Opcode = ST.isRC32()        ? RISCC::LINK_S3_CALL32_LITERAL
                    : ST.hasLongJall() ? RISCC::LINK_S3_CALL16
                                       : RISCC::LINK_S3_CALL_MIN;
  I = MBB.insert(I, BuildMI(MF, DebugLoc(), get(Opcode))
                        .addGlobalAddress(M.getNamedValue(MF.getName())));
  return I;
}

static Register getStackAccess(const MachineInstr &MI, int &FrameIndex,
                               TypeSize &Bytes) {
  switch (MI.getOpcode()) {
  case RISCC::LD:
  case RISCC::ST:
  case RISCC::LD_NANO:
  case RISCC::ST_NANO:
    Bytes = TypeSize::getFixed(2);
    break;
  case RISCC::LD32:
  case RISCC::ST32:
    Bytes = TypeSize::getFixed(4);
    break;
  default:
    return Register();
  }
  if (!MI.getOperand(1).isFI() || !MI.getOperand(2).isImm() ||
      MI.getOperand(2).getImm() != 0 || MI.hasOrderedMemoryRef())
    return Register();
  FrameIndex = MI.getOperand(1).getIndex();
  return MI.getOperand(0).getReg();
}

Register RISCCInstrInfo::isLoadFromStackSlot(const MachineInstr &MI, int &FI,
                                             TypeSize &Bytes) const {
  return MI.mayLoad() ? getStackAccess(MI, FI, Bytes) : Register();
}

Register RISCCInstrInfo::isStoreToStackSlot(const MachineInstr &MI, int &FI,
                                            TypeSize &Bytes) const {
  return MI.mayStore() ? getStackAccess(MI, FI, Bytes) : Register();
}

Register RISCCInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                             int &FI) const {
  TypeSize Bytes = TypeSize::getZero();
  return isLoadFromStackSlot(MI, FI, Bytes);
}

Register RISCCInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                            int &FI) const {
  TypeSize Bytes = TypeSize::getZero();
  return isStoreToStackSlot(MI, FI, Bytes);
}

bool RISCCInstrInfo::getMemOperandsWithOffsetWidth(
    const MachineInstr &MI, SmallVectorImpl<const MachineOperand *> &BaseOps,
    int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
    const TargetRegisterInfo *) const {
  unsigned Bytes;
  bool HasOffset = false;
  switch (MI.getOpcode()) {
  default:
    return false;
  case RISCC::LD:
  case RISCC::ST:
    Bytes = 2;
    HasOffset = true;
    break;
  case RISCC::LD32:
  case RISCC::ST32:
    Bytes = 4;
    HasOffset = true;
    break;
  case RISCC::LDB:
  case RISCC::LDBS:
  case RISCC::STB:
  case RISCC::LDB32:
  case RISCC::LDBS32:
  case RISCC::STB32:
    Bytes = 1;
    break;
  case RISCC::LDH:
  case RISCC::LDHS:
  case RISCC::STH:
    Bytes = 2;
    break;
  }
  if (HasOffset && !MI.getOperand(2).isImm())
    return false;
  BaseOps.push_back(&MI.getOperand(1));
  Offset = HasOffset ? MI.getOperand(2).getImm() : 0;
  OffsetIsScalable = false;
  Width = LocationSize::precise(Bytes);
  return true;
}

bool RISCCInstrInfo::areMemAccessesTriviallyDisjoint(
    const MachineInstr &A, const MachineInstr &B) const {
  if (A.hasUnmodeledSideEffects() || B.hasUnmodeledSideEffects() ||
      A.hasOrderedMemoryRef() || B.hasOrderedMemoryRef())
    return false;
  SmallVector<const MachineOperand *, 1> BaseA, BaseB;
  int64_t OffsetA, OffsetB;
  bool Scalable;
  LocationSize WidthA = LocationSize::precise(0);
  LocationSize WidthB = LocationSize::precise(0);
  if (!getMemOperandsWithOffsetWidth(A, BaseA, OffsetA, Scalable, WidthA,
                                     &RI) ||
      !getMemOperandsWithOffsetWidth(B, BaseB, OffsetB, Scalable, WidthB,
                                     &RI) ||
      !BaseA.front()->isIdenticalTo(*BaseB.front()))
    return false;
  // Different fields of the same object can be scheduled independently.
  return OffsetA <= OffsetB ? OffsetB - OffsetA >= int64_t(WidthA.getValue())
                            : OffsetA - OffsetB >= int64_t(WidthB.getValue());
}

void RISCCInstrInfo::materializeImmediate(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator I,
                                          const DebugLoc &DL,
                                          Register Destination, int64_t Value,
                                          MachineInstr::MIFlag Flag) const {
  auto Sequence = RISCCMatInt::generate(
      APInt(64, static_cast<uint64_t>(Value)).trunc(STI.isRC32() ? 32 : 16),
      STI, 1);
  if (!Sequence.empty()) {
    BuildMI(MBB, I, DL, get(getNativeOpcode(Sequence.front().Opcode)),
            Destination)
        .addImm(Sequence.front().Immediate)
        .setMIFlag(Flag);
    return;
  }
  if (STI.isRC32()) {
    MachineFunction &MF = *MBB.getParent();
    auto *C = ConstantInt::get(Type::getInt32Ty(MF.getFunction().getContext()),
                               Value);
    unsigned CPI = MF.getConstantPool()->getConstantPoolIndex(C, Align(4));
    BuildMI(MBB, I, DL, get(RISCC::LDPC), Destination)
        .addConstantPoolIndex(CPI)
        .addMemOperand(
            MF.getMachineMemOperand(MachinePointerInfo::getConstantPool(MF),
                                    MachineMemOperand::MOLoad, 4, Align(4)))
        .setMIFlag(Flag);
    return;
  }
  BuildMI(MBB, I, DL, get(RISCC::LDI16), Destination)
      .addImm(Value)
      .setMIFlag(Flag);
}

// sext8(x + C) = ((x + ((C & 255) - 128)) & 255) - 128.
// Fold only after allocation so the biased intermediate does not lengthen
// live ranges or disrupt coalescing. No intervening use may observe it.
static bool foldByteExtensionBias(MachineInstr &Extend,
                                  const RISCCRegisterInfo &RI) {
  Register Src = Extend.getOperand(1).getReg();
  // Updating the same register also proves the old sum is dead afterwards.
  if (Extend.getOperand(0).getReg() != Src)
    return false;
  auto &MBB = *Extend.getParent();
  for (auto I = Extend.getIterator(); I != MBB.begin();) {
    MachineInstr &MI = *--I;
    if (MI.isCall() || MI.isTerminator() || MI.isInlineAsm() ||
        MI.isCFIInstruction() || MI.hasUnmodeledSideEffects())
      return false;
    if (MI.modifiesRegister(Src, &RI)) {
      if ((MI.getOpcode() != RISCC::ADDI && MI.getOpcode() != RISCC::ADDI32) ||
          MI.getOperand(0).getReg() != Src || MI.getOperand(1).getReg() != Src ||
          MI.getFlag(MachineInstr::FrameSetup) ||
          MI.getFlag(MachineInstr::FrameDestroy))
        return false;
      int64_t Bias = (MI.getOperand(2).getImm() & 255) - 128;
      if (Bias) {
        MI.getOperand(2).setImm(Bias);
        MI.clearFlag(MachineInstr::NoSWrap);
        MI.clearFlag(MachineInstr::NoUWrap);
      } else {
        MI.eraseFromParent();
      }
      return true;
    }
    if (MI.readsRegister(Src, &RI))
      return false;
  }
  return false;
}

bool RISCCInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();
  if (Opcode != RISCC::SEXT8_RC16 && Opcode != RISCC::SEXT8_RC32 &&
      Opcode != RISCC::SEXT16_RC32 && Opcode != RISCC::SEXT16_SHIFT_RC32)
    return false;

  MachineBasicBlock &MBB = *MI.getParent();
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  const DebugLoc &DL = MI.getDebugLoc();
  if (Opcode == RISCC::SEXT16_SHIFT_RC32) {
    for (unsigned Shift :
         {RISCC::SLLI32, RISCC::SLLI32, RISCC::SRAI32, RISCC::SRAI32}) {
      BuildMI(MBB, MI, DL, get(Shift), Dst).addReg(Src).addImm(8);
      Src = Dst;
    }
    MI.eraseFromParent();
    return true;
  }
  if (Opcode == RISCC::SEXT16_RC32) {
    materializeImmediate(MBB, MI, DL, RISCC::R0, 0xffff);
    BuildMI(MBB, MI, DL, get(RISCC::AND32), Dst).addReg(Src).addReg(RISCC::R0);
    materializeImmediate(MBB, MI, DL, RISCC::R0, 0x8000);
    BuildMI(MBB, MI, DL, get(RISCC::XOR32), Dst).addReg(Dst).addReg(RISCC::R0);
    BuildMI(MBB, MI, DL, get(RISCC::SUB32), Dst).addReg(Dst).addReg(RISCC::R0);
    MI.eraseFromParent();
    return true;
  }

  bool Biased = foldByteExtensionBias(MI, RI);
  bool SourceIsByteLoad = false;
  for (auto I = MI.getIterator(); I != MBB.begin();) {
    --I;
    if (!I->modifiesRegister(Src, &RI))
      continue;
    SourceIsByteLoad = I->getOpcode() == getNativeOpcode(RISCC::LDB) &&
                       I->getOperand(0).getReg() == Src;
    break;
  }
  Register Extended = Src;
  if (!SourceIsByteLoad) {
    BuildMI(MBB, MI, DL, get(getNativeOpcode(RISCC::ANDI)), Dst)
        .addReg(Src)
        .addImm(0xff);
    Extended = Dst;
  }
  if (!Biased)
    BuildMI(MBB, MI, DL, get(getNativeOpcode(RISCC::XORI)), Dst)
        .addReg(Extended)
        .addImm(0x80);
  BuildMI(MBB, MI, DL, get(getNativeOpcode(RISCC::ADDI)), Dst)
      .addReg(Dst)
      .addImm(-128);
  MI.eraseFromParent();
  return true;
}

void RISCCInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, Register Dst, Register Src,
                                 bool Kill, bool RenamableDst,
                                 bool RenamableSrc) const {
  const TargetRegisterClass *GPR = STI.getGPRClass();
  const TargetRegisterClass *SReg = STI.getSRegClass();
  unsigned Opcode;
  if (GPR->contains(Dst, Src))
    Opcode = getNativeOpcode(RISCC::MOV);
  else if (GPR->contains(Dst) && SReg->contains(Src))
    Opcode = RISCC::MFS;
  else if (SReg->contains(Dst) && GPR->contains(Src))
    Opcode = RISCC::MTS;
  else
    llvm_unreachable("unsupported RISC-C physical-register copy");
  BuildMI(MBB, I, DL, get(Opcode))
      .addReg(Dst, RegState::Define | getRenamableRegState(RenamableDst))
      .addReg(Src, getKillRegState(Kill) | getRenamableRegState(RenamableSrc));
}

std::optional<DestSourcePair>
RISCCInstrInfo::isCopyInstrImpl(const MachineInstr &MI) const {
  if (MI.isMoveReg())
    return DestSourcePair{MI.getOperand(0), MI.getOperand(1)};
  return std::nullopt;
}

bool RISCCInstrInfo::simplifyInstruction(MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();
  bool Changed = false;
  if ((Opcode == RISCC::OR || Opcode == RISCC::OR32 || Opcode == RISCC::AND ||
       Opcode == RISCC::AND32) &&
      MI.getNumOperands() == 3 &&
      MI.getOperand(1).getReg() == MI.getOperand(2).getReg() &&
      MI.getOperand(1).getSubReg() == MI.getOperand(2).getSubReg() &&
      !MI.getOperand(1).isUndef() && !MI.getOperand(2).isUndef()) {
    // Copy propagation can make the two inputs identical after allocation.
    // Normalize to an actual move before the copy tracker observes it.
    Opcode = Opcode == RISCC::OR32 || Opcode == RISCC::AND32 ? RISCC::MOV32
                                                             : RISCC::MOV;
    MI.removeOperand(2);
    MI.getOperand(1).setIsKill(false);
    MI.setDesc(get(Opcode));
    Changed = true;
  }
  if ((Opcode == RISCC::MOV || Opcode == RISCC::MOV32) &&
      MI.getNumOperands() == 2 &&
      MI.getOperand(0).getReg() == MI.getOperand(1).getReg() &&
      MI.getOperand(0).getSubReg() == MI.getOperand(1).getSubReg()) {
    // Preserve liveness bookkeeping without emitting a self move. The
    // generic post-RA expander uses the same representation for identity
    // subregister copies, and branch relaxation accounts for its zero size.
    MI.setDesc(get(TargetOpcode::KILL));
    return true;
  }
  return Changed;
}

void RISCCInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator I,
                                         Register Src, bool Kill, int FI,
                                         const TargetRegisterClass *RC,
                                         Register,
                                         MachineInstr::MIFlag Flags) const {
  const bool GPR = STI.getGPRClass()->hasSubClassEq(RC);
  const bool SReg = STI.getSRegClass()->hasSubClassEq(RC);
  assert((GPR || SReg) && "only GPR and S-register spills are supported");
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOStore,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));
  if (SReg) {
    BuildMI(MBB, I, DebugLoc(), get(RISCC::MFS), RISCC::R0)
        .addReg(Src, getKillRegState(Kill));
    Src = RISCC::R0;
    Kill = false;
  }
  unsigned Opcode = STI.isNano() && !SReg ? unsigned(RISCC::ST_NANO)
                                          : getNativeOpcode(RISCC::ST);
  BuildMI(MBB, I, DebugLoc(), get(Opcode))
      .addReg(Src, getKillRegState(Kill))
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMO)
      .setMIFlag(Flags);
}

void RISCCInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator I,
                                          Register Dst, int FI,
                                          const TargetRegisterClass *RC,
                                          Register, unsigned,
                                          MachineInstr::MIFlag Flags) const {
  const bool GPR = STI.getGPRClass()->hasSubClassEq(RC);
  const bool SReg = STI.getSRegClass()->hasSubClassEq(RC);
  assert((GPR || SReg) && "only GPR and S-register reloads are supported");
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOLoad,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));
  unsigned Opcode = STI.isNano() && !SReg ? unsigned(RISCC::LD_NANO)
                                          : getNativeOpcode(RISCC::LD);
  BuildMI(MBB, I, DebugLoc(), get(Opcode), SReg ? Register(RISCC::R0) : Dst)
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMO)
      .setMIFlag(Flags);
  if (SReg)
    BuildMI(MBB, I, DebugLoc(), get(RISCC::MTS), Dst).addReg(RISCC::R0);
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

bool RISCCInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  TBB = FBB = nullptr;
  Cond.clear();
  for (auto I = MBB.getFirstTerminator(); I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;
    bool Unconditional =
        I->getOpcode() == RISCC::JMP8 || I->getOpcode() == RISCC::JMP16;
    if ((!Unconditional && !isConditionalBranchOpcode(I->getOpcode())) ||
        !I->getOperand(0).isMBB())
      return true;

    if (Unconditional) {
      // Nothing after an unconditional transfer is reachable.
      if (AllowModify)
        MBB.erase(std::next(I), MBB.end());
      else if (llvm::any_of(
                   make_range(std::next(I), MBB.end()),
                   [](const MachineInstr &MI) { return !MI.isDebugInstr(); }))
        return true;
      (Cond.empty() ? TBB : FBB) = I->getOperand(0).getMBB();
      return false;
    }
    // The generic branch interface represents at most one condition. Never
    // silently drop an earlier branch from a multi-way terminator sequence.
    if (!Cond.empty())
      return true;
    TBB = I->getOperand(0).getMBB();
    Cond.push_back(MachineOperand::CreateImm(I->getOpcode()));
  }
  return false;
}

unsigned RISCCInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  unsigned Count = 0, Bytes = 0;
  while (!MBB.empty()) {
    auto I = MBB.getLastNonDebugInstr();
    if (I == MBB.end() ||
        (!isConditionalBranchOpcode(I->getOpcode()) &&
         I->getOpcode() != RISCC::JMP8 && I->getOpcode() != RISCC::JMP16))
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
  unsigned Opcode = Cond.empty() ? RISCC::JMP8 : Cond[0].getImm();
  BuildMI(&MBB, DL, get(Opcode)).addMBB(TBB);
  unsigned Count = 1;
  if (!Cond.empty() && FBB) {
    BuildMI(&MBB, DL, get(RISCC::JMP8)).addMBB(FBB);
    ++Count;
  }
  if (BytesAdded)
    *BytesAdded = 2 * Count;
  return Count;
}

bool RISCCInstrInfo::isBranchOffsetInRange(unsigned Opcode,
                                           int64_t BrOffset) const {
  if (Opcode == RISCC::JMP16)
    return true;
  if (Opcode != RISCC::JMP8 && !isConditionalBranchOpcode(Opcode))
    return true;
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

void RISCCInstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                          MachineBasicBlock &DestBB,
                                          MachineBasicBlock &RestoreBB,
                                          const DebugLoc &DL, int64_t,
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
      *BuildMI(MBB, MBB.end(), DL,
               get(STI.isRC32() ? RISCC::LDPC_BRANCH : RISCC::LDI16),
               VirtualScratch)
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
    Address.getOperand(1).setMBB(&RestoreBB);
    if (!MF.getFrameInfo().getSavePoints().empty()) {
      // A shrink-wrapped path may not have a function frame yet. Preserve
      // the scratch in a balanced temporary slot below the current SP,
      // independent of whether the path has executed the prologue. The
      // restore block immediately undoes the adjustment before the target.
      int Slot = STI.getSlotSize();
      BuildMI(MBB, Address, DL, get(getNativeOpcode(RISCC::ADDI)), RISCC::R7)
          .addReg(RISCC::R7)
          .addImm(-Slot);
      BuildMI(MBB, Address, DL, get(getNativeOpcode(RISCC::ST)))
          .addReg(Scratch, RegState::Kill)
          .addReg(RISCC::R7)
          .addImm(0);
      BuildMI(RestoreBB, RestoreBB.end(), DL, get(getNativeOpcode(RISCC::LD)),
              Scratch)
          .addReg(RISCC::R7)
          .addImm(0);
      BuildMI(RestoreBB, RestoreBB.end(), DL, get(getNativeOpcode(RISCC::ADDI)),
              RISCC::R7)
          .addReg(RISCC::R7)
          .addImm(Slot);
    } else {
      int FI =
          MF.getInfo<RISCCMachineFunctionInfo>()->getBranchRelaxationSpillFI();
      if (FI < 0)
        report_fatal_error("RISC-C function size was underestimated");

      storeRegToStackSlot(MBB, Address.getIterator(), Scratch, true, FI,
                          STI.getGPRClass(), Register(), MachineInstr::NoFlags);
      STI.getRegisterInfo()->eliminateFrameIndex(
          std::prev(Address.getIterator()), 0, 1, RS);

      loadRegFromStackSlot(RestoreBB, RestoreBB.end(), Scratch, FI,
                           STI.getGPRClass(), Register(), 0,
                           MachineInstr::NoFlags);
      STI.getRegisterInfo()->eliminateFrameIndex(RestoreBB.back(), 0, 1, RS);
    }
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
