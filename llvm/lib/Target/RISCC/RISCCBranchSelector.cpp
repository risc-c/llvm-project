#include "RISCC.h"
#include "RISCCInstrInfo.h"
#include "RISCCSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "riscc-branch-select"

namespace {
class RISCCBranchSelectorLegacy final : public MachineFunctionPass {
public:
  static char ID;
  RISCCBranchSelectorLegacy() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "RISC-C branch selector"; }
  bool runOnMachineFunction(MachineFunction &) override;
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};
}

char RISCCBranchSelectorLegacy::ID = 0;

static bool isShortConditional(unsigned Opcode) {
  return Opcode == RISCC::BEQZ || Opcode == RISCC::BNEZ ||
         Opcode == RISCC::BLTZ || Opcode == RISCC::BGEZ;
}

static bool runBranchSelector(MachineFunction &MF) {
  const auto &TII = *MF.getSubtarget<RISCCSubtarget>().getInstrInfo();
  bool Changed = false;
  for (;;) {
    DenseMap<const MachineBasicBlock *, uint64_t> BlockOffset;
    DenseMap<const MachineInstr *, uint64_t> InstOffset;
    uint64_t Offset = 0;
    for (MachineBasicBlock &MBB : MF) {
      BlockOffset[&MBB] = Offset;
      for (MachineInstr &MI : MBB) {
        InstOffset[&MI] = Offset;
        Offset += TII.getInstSizeInBytes(MI);
      }
    }

    bool Expanded = false;
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : make_early_inc_range(MBB)) {
        const unsigned Opcode = MI.getOpcode();
        if (Opcode != RISCC::JMP8 && !isShortConditional(Opcode))
          continue;
        if (!MI.getOperand(0).isMBB())
          continue;
        int64_t Delta = int64_t(BlockOffset[MI.getOperand(0).getMBB()]) -
                        int64_t(InstOffset[&MI] + 2);
        if ((Delta & 1) == 0 && isInt<8>(Delta / 2))
          continue;
        if (Opcode == RISCC::JMP8) {
          MI.setDesc(TII.get(RISCC::JMP16));
        } else {
          MachineBasicBlock *Destination = MI.getOperand(0).getMBB();
          MI.setDesc(TII.get(RISCC::LONG_BR));
          MI.getOperand(0).ChangeToImmediate(Opcode);
          MI.addOperand(MachineOperand::CreateMBB(Destination));
        }
        Expanded = Changed = true;
        break;
      }
      if (Expanded)
        break;
    }
    if (!Expanded)
      break;
  }
  return Changed;
}

bool RISCCBranchSelectorLegacy::runOnMachineFunction(MachineFunction &MF) {
  return runBranchSelector(MF);
}

PreservedAnalyses RISCCBranchSelectorPass::run(
    MachineFunction &MF, MachineFunctionAnalysisManager &) {
  return runBranchSelector(MF) ? getMachineFunctionPassPreservedAnalyses()
                               : PreservedAnalyses::all();
}

INITIALIZE_PASS(RISCCBranchSelectorLegacy, DEBUG_TYPE, "RISC-C branch selector",
                false, false)

FunctionPass *llvm::createRISCCBranchSelectorPass() {
  return new RISCCBranchSelectorLegacy();
}
