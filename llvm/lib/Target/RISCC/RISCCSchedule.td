//===-- RISCCSchedule.td - RISC-C scheduling ----------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Prefer independent work between a load and its consumer. The ISA profiles
// also run on multicycle cores, where reordering does not change load latency.
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

let SchedModel = RISCCSchedModel in {
  def RISCCPipe : ProcResource<1>;
  def : WriteRes<WriteRISCCALU, [RISCCPipe]>;
  def : WriteRes<WriteRISCCLoad, [RISCCPipe]> { let Latency = 2; }
}
