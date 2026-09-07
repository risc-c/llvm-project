//===-- RISCCRegisterInfo.td - RISCC Registers -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class RISCCReg<bits<16> enc, string name> : Register<name> {
  let Namespace = "RISCC";
  let HWEncoding = enc;
  let DwarfNumbers = [enc];
}

def R0 : RISCCReg<0, "r0">;
def R1 : RISCCReg<1, "r1">;
def R2 : RISCCReg<2, "r2">;
def R3 : RISCCReg<3, "r3">;
def R4 : RISCCReg<4, "r4">;
def R5 : RISCCReg<5, "r5">;
def R6 : RISCCReg<6, "r6">;
def R7 : RISCCReg<7, "r7">;

def S0 : RISCCReg<8,  "s0">;
def S1 : RISCCReg<9,  "s1">;
def S2 : RISCCReg<10, "s2">;
def S3 : RISCCReg<11, "s3">;
def S4 : RISCCReg<12, "s4">;
def S5 : RISCCReg<13, "s5">;
def S6 : RISCCReg<14, "s6">;
def S7 : RISCCReg<15, "s7">;

def GPR : RegisterClass<"RISCC", [i16], 16,
  (add R0, R1, R2, R3, R4, R5, R6, R7)>;
def GPR32 : RegisterClass<"RISCC", [i32], 32,
  (add R0, R1, R2, R3, R4, R5, R6, R7)>;
// Mainline caller-saved GPRs.
def GPRCaller : RegisterClass<"RISCC", [i16], 16,
  (add R0, R1, R2, R3)>;
// Keep Nano's incoming link in caller-saved registers or a normal spill slot.
// This avoids conflating the copied link with a callee-saved register's own
// incoming value when the prologue/epilogue pass saves that register.
def GPRNanoCaller : RegisterClass<"RISCC", [i16], 16,
  (add R0, R1, R2, R3, R6)>;
def SREG : RegisterClass<"RISCC", [i16], 16,
  (add S0, S1, S2, S3, S4, S5, S6, S7)>;
def SREG32 : RegisterClass<"RISCC", [i32], 32,
  (add S0, S1, S2, S3, S4, S5, S6, S7)>;

def CSR_RISCC : CalleeSavedRegs<(add R4, R5, R6, S5, S6)>;
def CSR_RISCC_CallPreserved : CalleeSavedRegs<(add R4, R5, R6, R7, S5, S6)>;
def CSR_RISCC_SRegPreservingCall :
    CalleeSavedRegs<(add R4, R5, R6, R7, S2, S3, S4, S5, S6)>;
def CSR_RISCC_Nano : CalleeSavedRegs<(add R4, R5)>;
def CSR_RISCC_Nano_CallPreserved : CalleeSavedRegs<(add R4, R5, R7)>;
