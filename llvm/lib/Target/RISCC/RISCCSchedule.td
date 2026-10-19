//===-- RISCCSchedule.td - RISC-C scheduling ----------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Tune the shared ISA profiles for the Cached pipeline with local SRAM or
// cache hits. Other implementations execute the same ISA with different
// timings. Branches consume R0 in Execute: use the ordinary data latency.
def RISCCSchedModel : SchedMachineModel {
  let IssueWidth = 1;
  let MicroOpBufferSize = 0;
  let LoadLatency = 2;
  let MispredictPenalty = 2;
  let CompleteModel = 0;
  let PostRAScheduler = 1;
}

def WriteRISCCALU : SchedWrite;
def WriteRISCCLoad : SchedWrite;
def WriteRISCCMul : SchedWrite;

let SchedModel = RISCCSchedModel in {
  def RISCCPipe : ProcResource<1>;
  def : WriteRes<WriteRISCCALU, [RISCCPipe]>;
  def : WriteRes<WriteRISCCLoad, [RISCCPipe]> { let Latency = 2; }
  // Operand capture, DSP multiplication, then sum/writeback. Execute is
  // occupied throughout: this is not a pipelined multiplier that can overlap
  // the intervening cycles with independent instructions.
  def : WriteRes<WriteRISCCMul, [RISCCPipe]> {
    let Latency = 3;
    let ReleaseAtCycles = [3];
    // Account for the three occupied issue clocks of the blocking state
    // machine. With IssueWidth=1 and no micro-op buffer this prevents another
    // instruction issuing in either intervening clock. It remains one
    // architectural instruction, not three instructions for code-size costs.
    let NumMicroOps = 3;
  }
}
