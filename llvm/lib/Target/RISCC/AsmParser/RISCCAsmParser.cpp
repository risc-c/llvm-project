//===-- RISCCAsmParser.cpp - RISCC Assembly Parser ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCCMCExpr.h"
#include "MCTargetDesc/RISCCMCTargetDesc.h"
#include "TargetInfo/RISCCTargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Casting.h"
#include <optional>

using namespace llvm;

namespace {
class RISCCOperand final : public MCParsedAsmOperand {
  enum KindTy { Token, Reg, Imm } Kind;
  std::string Tok;
  MCRegister RegNo;
  const MCExpr *Expr = nullptr;
  bool KeepExpr = false;
  SMLoc Start, End;

  RISCCOperand(KindTy K, SMLoc S, SMLoc E) : Kind(K), Start(S), End(E) {}

public:
  static std::unique_ptr<RISCCOperand> token(StringRef T, SMLoc L) {
    auto Op = std::unique_ptr<RISCCOperand>(new RISCCOperand(Token, L, L));
    Op->Tok = T;
    return Op;
  }
  static std::unique_ptr<RISCCOperand> reg(MCRegister R, SMLoc S, SMLoc E) {
    auto Op = std::unique_ptr<RISCCOperand>(new RISCCOperand(Reg, S, E));
    Op->RegNo = R;
    return Op;
  }
  static std::unique_ptr<RISCCOperand> imm(const MCExpr *E, SMLoc S, SMLoc L,
                                           bool Keep = false) {
    auto Op = std::unique_ptr<RISCCOperand>(new RISCCOperand(Imm, S, L));
    Op->Expr = E;
    Op->KeepExpr = Keep;
    return Op;
  }

  bool isToken() const override { return Kind == Token; }
  bool isReg() const override { return Kind == Reg; }
  bool isImm() const override { return Kind == Imm; }
  bool isMem() const override { return false; }
  bool isU8Imm() const { return isIntInRange(0, 255); }
  bool isS8Imm() const { return isIntInRange(-128, 127); }
  bool isShiftImm() const { return isIntInRange(1, 8); }
  bool isU16Imm() const { return isIntInRange(0, 65535); }

  bool isIntInRange(int64_t Min, int64_t Max) const {
    if (!isImm())
      return false;
    int64_t V;
    if (isa<RISCCMCExpr>(Expr))
      return true;
    return !Expr->evaluateAsAbsolute(V) || (V >= Min && V <= Max);
  }

  StringRef getToken() const { return Tok; }
  MCRegister getReg() const override { return RegNo; }
  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  void addRegOperands(MCInst &MI, unsigned N) const {
    assert(N == 1 && isReg());
    MI.addOperand(MCOperand::createReg(RegNo));
  }
  void addImmOperands(MCInst &MI, unsigned N) const {
    assert(N == 1 && isImm());
    if (!KeepExpr) {
      int64_t Value;
      if (Expr->evaluateAsAbsolute(Value)) {
        MI.addOperand(MCOperand::createImm(Value));
        return;
      }
    }
    MI.addOperand(MCOperand::createExpr(Expr));
  }

  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    if (isToken()) {
      OS << "Token " << Tok;
    } else if (isReg()) {
      OS << "Reg " << RegNo;
    } else {
      OS << "Imm ";
      MAI.printExpr(OS, *Expr);
    }
  }
};

class RISCCAsmParser final : public MCTargetAsmParser {
  MCAsmParser &Parser;
  std::string CurrentMnemonic;

#define GET_ASSEMBLER_HEADER
#include "RISCCGenAsmMatcher.inc"

  bool parseOperand(OperandVector &Operands);
  bool parseMemory(OperandVector &Operands);

public:
  enum RISCCMatchResultTy {
    Match_Dummy = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "RISCCGenAsmMatcher.inc"
#undef GET_OPERAND_DIAGNOSTIC_TYPES
  };

  RISCCAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                 const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }

  bool matchAndEmitInstruction(SMLoc, unsigned &, OperandVector &,
                               MCStreamer &, uint64_t &,
                               bool) override;
  bool parseInstruction(ParseInstructionInfo &, StringRef, SMLoc,
                        OperandVector &) override;
  bool parseRegister(MCRegister &, SMLoc &, SMLoc &) override;
  ParseStatus tryParseRegister(MCRegister &, SMLoc &, SMLoc &) override;
  ParseStatus parseDirective(AsmToken) override { return ParseStatus::NoMatch; }
  bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) override;
};
}

static MCRegister MatchRegisterName(StringRef Name);

bool RISCCAsmParser::parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) {
  if (Parser.getTok().is(AsmToken::Identifier) &&
      Parser.getLexer().peekTok().is(AsmToken::LParen)) {
    std::optional<RISCCMCExpr::VariantKind> Kind =
        RISCCMCExpr::parseVariantKind(Parser.getTok().getIdentifier());
    if (!Kind)
      return Parser.parsePrimaryExpr(Res, EndLoc, nullptr);
    Parser.Lex();
    if (Parser.parseToken(AsmToken::LParen, "expected '('") ||
        Parser.parseExpression(Res))
      return true;
    EndLoc = Parser.getTok().getEndLoc();
    if (Parser.parseToken(AsmToken::RParen, "expected ')'"))
      return true;
    Res = RISCCMCExpr::create(*Kind, Res, getContext());
    return false;
  }
  return Parser.parsePrimaryExpr(Res, EndLoc, nullptr);
}

ParseStatus RISCCAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &S,
                                             SMLoc &E) {
  if (!Parser.getTok().is(AsmToken::Identifier))
    return ParseStatus::NoMatch;
  StringRef Name = Parser.getTok().getIdentifier();
  Reg = MatchRegisterName(Name.lower());
  if (!Reg)
    return ParseStatus::NoMatch;
  S = Parser.getTok().getLoc();
  E = Parser.getTok().getEndLoc();
  Parser.Lex();
  return ParseStatus::Success;
}

bool RISCCAsmParser::parseRegister(MCRegister &Reg, SMLoc &S, SMLoc &E) {
  ParseStatus Status = tryParseRegister(Reg, S, E);
  if (Status.isSuccess())
    return false;
  return Error(Parser.getTok().getLoc(), "expected RISC-C register");
}

bool RISCCAsmParser::parseMemory(OperandVector &Operands) {
  SMLoc LBracLoc = Parser.getTok().getLoc();
  if (Parser.parseToken(AsmToken::LBrac, "expected '['"))
    return true;
  Operands.push_back(RISCCOperand::token("[", LBracLoc));
  MCRegister Base;
  SMLoc S, E;
  if (parseRegister(Base, S, E))
    return true;
  Operands.push_back(RISCCOperand::reg(Base, S, E));

  if (Parser.getTok().is(AsmToken::RBrac)) {
    if (CurrentMnemonic != "stb") {
      Operands.push_back(RISCCOperand::token("+", E));
      Operands.push_back(RISCCOperand::imm(
          MCConstantExpr::create(0, getContext()), E, E));
    }
  } else {
    bool Negative = Parser.getTok().is(AsmToken::Minus);
    if (!Parser.getTok().is(AsmToken::Plus) && !Negative)
      return Error(Parser.getTok().getLoc(), "expected '+' or ']' in address");
    SMLoc ES = Parser.getTok().getLoc();
    Parser.Lex();
    // Normalize both `[base + expr]` and `[base - expr]` to the token stream
    // described by the TableGen spelling: `[`, base, `+`, displacement, `]`.
    Operands.push_back(RISCCOperand::token("+", ES));
    if (CurrentMnemonic == "ldwx" || CurrentMnemonic == "ldb" ||
        CurrentMnemonic == "ldbs") {
      if (Negative)
        return Error(ES, "indexed address requires '+' and a register");
      MCRegister Index;
      SMLoc IS, IE;
      if (parseRegister(Index, IS, IE))
        return true;
      Operands.push_back(RISCCOperand::reg(Index, IS, IE));
      SMLoc RBracLoc = Parser.getTok().getLoc();
      if (Parser.parseToken(AsmToken::RBrac, "expected ']'"))
        return true;
      Operands.push_back(RISCCOperand::token("]", RBracLoc));
      return false;
    }
    if (Parser.getTok().is(AsmToken::Identifier) &&
        MatchRegisterName(Parser.getTok().getIdentifier().lower()))
      return Error(ES, "register-indexed word loads use LDWX");
    const MCExpr *Expr;
    if (Parser.parseExpression(Expr))
      return true;
    if (Negative)
      Expr = MCUnaryExpr::createMinus(Expr, getContext());
    Operands.push_back(RISCCOperand::imm(Expr, ES, Parser.getTok().getEndLoc()));
  }
  SMLoc RBracLoc = Parser.getTok().getLoc();
  if (Parser.parseToken(AsmToken::RBrac, "expected ']'"))
    return true;
  Operands.push_back(RISCCOperand::token("]", RBracLoc));
  return false;
}

bool RISCCAsmParser::parseOperand(OperandVector &Operands) {
  if (Parser.getTok().is(AsmToken::LBrac))
    return parseMemory(Operands);

  if (Parser.getTok().is(AsmToken::Identifier)) {
    MCRegister R;
    SMLoc S, E;
    if (tryParseRegister(R, S, E).isSuccess()) {
      Operands.push_back(RISCCOperand::reg(R, S, E));
      return false;
    }
  }

  SMLoc S = Parser.getTok().getLoc();
  const MCExpr *Expr;
  if (Parser.parseExpression(Expr))
    return Error(S, "expected register, immediate, or expression");
  bool IsBranch = CurrentMnemonic == "beqz" || CurrentMnemonic == "bnez" ||
                  CurrentMnemonic == "bltz" || CurrentMnemonic == "bgez" ||
                  CurrentMnemonic == "jmp8";
  Operands.push_back(RISCCOperand::imm(Expr, S, Parser.getTok().getEndLoc(),
                                       IsBranch));
  return false;
}

bool RISCCAsmParser::parseInstruction(ParseInstructionInfo &, StringRef Name,
                                      SMLoc NameLoc,
                                      OperandVector &Operands) {
  std::string Lower = Name.lower();
  CurrentMnemonic = Lower;
  Operands.push_back(RISCCOperand::token(Lower, NameLoc));

  if (Parser.getTok().is(AsmToken::EndOfStatement))
    return false;
  while (true) {
    if (parseOperand(Operands))
      return true;
    if (!Parser.getTok().is(AsmToken::Comma))
      break;
    Parser.Lex();
  }
  if (!Parser.getTok().is(AsmToken::EndOfStatement))
    return Error(Parser.getTok().getLoc(), "unexpected token in instruction");
  Parser.Lex();
  return false;
}

bool RISCCAsmParser::matchAndEmitInstruction(
    SMLoc Loc, unsigned &, OperandVector &Operands, MCStreamer &Out,
    uint64_t &ErrorInfo, bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned Result = MatchInstructionImpl(Operands, Inst, ErrorInfo,
                                         MatchingInlineAsm);
  if (Result == Match_Success) {
    if ((Inst.getOpcode() == RISCC::LDW ||
         Inst.getOpcode() == RISCC::STW ||
         Inst.getOpcode() == RISCC::LDW_NANO ||
         Inst.getOpcode() == RISCC::STW_NANO) &&
        Inst.getOperand(Inst.getNumOperands() - 1).isImm() &&
        (Inst.getOperand(Inst.getNumOperands() - 1).getImm() & 1))
      return Error(Loc, "word displacement must be even");
    if ((Inst.getOpcode() == RISCC::SHLI &&
         !STI->hasFeature(RISCC::FeatureWideShift)) ||
        ((Inst.getOpcode() == RISCC::SHRI || Inst.getOpcode() == RISCC::SARI) &&
         Inst.getOperand(2).isImm() && Inst.getOperand(2).getImm() != 1 &&
         !STI->hasFeature(RISCC::FeatureWideShift)))
      return Error(
          Loc, "instruction or shift count is unavailable in this profile");
    Inst.setLoc(Loc);
    Out.emitInstruction(Inst, *STI);
    return false;
  }
  if (Result == Match_MnemonicFail)
    return Error(Loc, "invalid RISC-C instruction mnemonic");
  SMLoc E = Loc;
  if (ErrorInfo < Operands.size())
    E = static_cast<RISCCOperand &>(*Operands[ErrorInfo]).getStartLoc();
  if (Result == Match_InvalidU8Imm)
    return Error(E, "immediate must be in the range 0..255");
  if (Result == Match_InvalidS8Imm)
    return Error(E, "immediate must be in the range -128..127");
  if (Result == Match_InvalidShiftImm)
    return Error(E, "shift amount must be in the range 1..8");
  if (Result == Match_InvalidU16Imm)
    return Error(E, "immediate must be in the range 0..65535");
  return Error(E, "invalid operand for RISC-C instruction");
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeRISCCAsmParser() {
  RegisterMCAsmParser<RISCCAsmParser> X(getTheRISCCTarget());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "RISCCGenAsmMatcher.inc"
