//===-- RISCCSRegLiveness.h - S-register liveness ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCC_RISCCSREGLIVENESS_H
#define LLVM_LIB_TARGET_RISCC_RISCCSREGLIVENESS_H

#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/MC/MCRegister.h"
#include <array>

namespace llvm {
class MachineFunction;
class MachineInstr;
class TargetRegisterInfo;

// Generic live-in repair omits reserved registers. Compute this small bank's
// liveness explicitly so placement and spill caching agree across CFG edges.
class RISCCSRegLiveness {
public:
  static constexpr std::array<MCPhysReg, 6> Registers = {
      RISCC::S2, RISCC::S3, RISCC::S4, RISCC::S5, RISCC::S6, RISCC::S7};
  struct Access {
    unsigned Uses = 0;
    unsigned Defs = 0;
    unsigned Before = 0;
    unsigned After = 0;
  };
  DenseMap<const MachineInstr *, Access> Instructions;

  RISCCSRegLiveness(const MachineFunction &, const TargetRegisterInfo &,
                    MCRegister Ignore = MCRegister());
};
} // namespace llvm

#endif
