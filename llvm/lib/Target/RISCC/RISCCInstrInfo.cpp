#include "RISCCInstrInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "RISCCGenInstrInfo.inc"

void RISCCInstrInfo::anchor() {}

RISCCInstrInfo::RISCCInstrInfo(const RISCCSubtarget &STI)
    : RISCCGenInstrInfo(STI, RI, RISCC::ADJCALLSTACKDOWN,
                        RISCC::ADJCALLSTACKUP) {}

void RISCCInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, Register Dst, Register Src,
                                 bool Kill, bool, bool) const {
  if (RISCC::GPRRegClass.contains(Dst, Src)) {
    BuildMI(MBB, I, DL, get(RISCC::MOV), Dst)
        .addReg(Src, getKillRegState(Kill));
    return;
  }
  if (RISCC::GPRRegClass.contains(Dst) && RISCC::SREGRegClass.contains(Src)) {
    BuildMI(MBB, I, DL, get(RISCC::MFS), Dst)
        .addReg(Src, getKillRegState(Kill));
    return;
  }
  if (RISCC::SREGRegClass.contains(Dst) && RISCC::GPRRegClass.contains(Src)) {
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
  assert(RC == &RISCC::GPRRegClass && "only GPR spills are supported");
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOStore,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));
  BuildMI(MBB, I, DebugLoc(), get(RISCC::STW))
      .addReg(Src, getKillRegState(Kill)).addFrameIndex(FI).addImm(0)
      .addMemOperand(MMO).setMIFlag(Flags);
}

void RISCCInstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register Dst,
    int FI, const TargetRegisterClass *RC, Register, unsigned,
    MachineInstr::MIFlag Flags) const {
  assert(RC == &RISCC::GPRRegClass && "only GPR spills are supported");
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOLoad,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));
  BuildMI(MBB, I, DebugLoc(), get(RISCC::LDW), Dst)
      .addFrameIndex(FI).addImm(0).addMemOperand(MMO).setMIFlag(Flags);
}

static bool isCondBranch(unsigned O) {
  return O == RISCC::BEQZ || O == RISCC::BNEZ || O == RISCC::BLTZ ||
         O == RISCC::BGEZ;
}

bool RISCCInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && Cond[0].isImm());
  switch (Cond[0].getImm()) {
  case RISCC::BEQZ: Cond[0].setImm(RISCC::BNEZ); return false;
  case RISCC::BNEZ: Cond[0].setImm(RISCC::BEQZ); return false;
  case RISCC::BLTZ: Cond[0].setImm(RISCC::BGEZ); return false;
  case RISCC::BGEZ: Cond[0].setImm(RISCC::BLTZ); return false;
  default: return true;
  }
}

bool RISCCInstrInfo::analyzeBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *&TBB, MachineBasicBlock *&FBB,
    SmallVectorImpl<MachineOperand> &Cond, bool AllowModify) const {
  auto I = MBB.getLastNonDebugInstr();
  if (I == MBB.end()) return false;
  if (I->getOpcode() == RISCC::JMP8 || I->getOpcode() == RISCC::JMP16) {
    if (!I->getOperand(0).isMBB()) return true;
    TBB = I->getOperand(0).getMBB();
    if (I == MBB.begin()) return false;
    --I;
    while (I->isDebugInstr() && I != MBB.begin()) --I;
    if (isCondBranch(I->getOpcode()) && I->getOperand(0).isMBB()) {
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(I->getOpcode()));
    }
    return false;
  }
  if (isCondBranch(I->getOpcode()) && I->getOperand(0).isMBB()) {
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
    if (I == MBB.end() || (!isCondBranch(I->getOpcode()) &&
                           I->getOpcode() != RISCC::JMP8 &&
                           I->getOpcode() != RISCC::JMP16))
      break;
    Bytes += getInstSizeInBytes(*I);
    I->eraseFromParent();
    ++Count;
  }
  if (BytesRemoved) *BytesRemoved = Bytes;
  return Count;
}

unsigned RISCCInstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  assert(TBB && Cond.size() <= 1);
  unsigned Count = 0, Bytes = 0;
  if (!Cond.empty()) {
    BuildMI(&MBB, DL, get(Cond[0].getImm())).addMBB(TBB);
    ++Count; Bytes += 2;
    if (FBB) { BuildMI(&MBB, DL, get(RISCC::JMP8)).addMBB(FBB); ++Count; Bytes += 2; }
  } else {
    BuildMI(&MBB, DL, get(RISCC::JMP8)).addMBB(TBB);
    ++Count; Bytes += 2;
  }
  if (BytesAdded) *BytesAdded = Bytes;
  return Count;
}

unsigned RISCCInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case TargetOpcode::CFI_INSTRUCTION: case TargetOpcode::EH_LABEL:
  case TargetOpcode::IMPLICIT_DEF: case TargetOpcode::KILL:
  case TargetOpcode::DBG_VALUE: return 0;
  case TargetOpcode::INLINEASM: case TargetOpcode::INLINEASM_BR:
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              MI.getMF()->getTarget().getMCAsmInfo());
  case TargetOpcode::BUNDLE: return getInstBundleSize(MI);
  default: return MI.getDesc().getSize();
  }
}
