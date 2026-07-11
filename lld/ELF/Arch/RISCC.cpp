//===- RISCC.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RISC-C is a little-endian 16-bit target with byte-addressed data and
// word-addressed code. ELF addresses remain byte based. Split-memory linker
// scripts use bit 16 as an ELF-only data-space tag; data relocations erase the
// tag while code relocations reject it and encode byte addresses divided by 2.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class RISCC final : public TargetInfo {
public:
  RISCC(Ctx &ctx) : TargetInfo(ctx) { defaultImageBase = 0; }
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void scanSection(InputSectionBase &sec) override;
  void validateOutput() const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;

private:
  void checkRelocationDomain(RelType type, const Symbol &s,
                             const InputSectionBase &site, uint64_t offset,
                             bool requiresCode) const;
  bool checkCodeAddress(uint8_t *loc, const Relocation &rel,
                        uint64_t val) const;
};
} // namespace

uint32_t RISCC::calcEFlags() const {
  if (ctx.objectFiles.empty())
    return 0;

  constexpr uint32_t knownMask = EF_RISCC_ABI_MASK | EF_RISCC_PROFILE_MASK;
  auto getFlags = [](InputFile *file) {
    return cast<ObjFile<ELF32LE>>(file)->getObj().getHeader().e_flags;
  };

  // Capability order is min < sys < full, although the provisional numeric
  // e_flags encodings are not ordered that way.
  unsigned outputRank = 0;
  for (InputFile *file : ctx.objectFiles) {
    uint32_t flags = getFlags(file);
    if (uint32_t unknown = flags & ~knownMask)
      ErrAlways(ctx) << file << ": unsupported RISC-C ELF flags 0x"
                     << utohexstr(unknown);

    uint32_t abi = flags & EF_RISCC_ABI_MASK;
    if (abi != EF_RISCC_ABI_V1)
      ErrAlways(ctx) << file << ": unsupported RISC-C ABI flags 0x"
                     << utohexstr(abi) << " (expected ABI v1)";

    unsigned rank = 0;
    switch (flags & EF_RISCC_PROFILE_MASK) {
    case EF_RISCC_PROFILE_MIN:
      rank = 1;
      break;
    case EF_RISCC_PROFILE_SYS:
      rank = 2;
      break;
    case EF_RISCC_PROFILE_FULL:
      rank = 3;
      break;
    case EF_RISCC_PROFILE_NANO:
      ErrAlways(ctx) << file
                     << ": RISC-C Nano uses an incompatible ABI and cannot "
                        "be linked as mainline RISC-C";
      break;
    default:
      ErrAlways(ctx) << file << ": unsupported RISC-C ISA profile flags 0x"
                     << utohexstr(flags & EF_RISCC_PROFILE_MASK);
      break;
    }
    outputRank = std::max(outputRank, rank);
  }

  uint32_t profile = outputRank == 3   ? EF_RISCC_PROFILE_FULL
                     : outputRank == 2 ? EF_RISCC_PROFILE_SYS
                                       : EF_RISCC_PROFILE_MIN;
  return EF_RISCC_ABI_V1 | profile;
}

void RISCC::checkRelocationDomain(RelType type, const Symbol &s,
                                  const InputSectionBase &site, uint64_t offset,
                                  bool requiresCode) const {
  // Undefined and absolute symbols have no output section. Their domain is
  // intentionally left to the final definition or to the explicit absolute
  // address checks in relocate(). For a section-relative definition, section
  // flags provide a layout-independent domain check in both unified and split
  // memory links.
  const OutputSection *section = s.getOutputSection();
  if (!section)
    return;

  bool isCode = section->flags & SHF_EXECINSTR;
  if (isCode == requiresCode)
    return;

  Err(ctx) << site.getLocation(offset) << ": relocation " << type
           << " against " << &s << " requires a "
           << (requiresCode ? "code" : "data")
           << " symbol, but the symbol is defined in "
           << (isCode ? "executable" : "non-executable") << " section "
           << section->name;
}

RelExpr RISCC::getRelExpr(RelType type, const Symbol &s,
                          const uint8_t *loc) const {
  switch (type) {
  case R_RISCC_NONE:
    return R_NONE;
  case R_RISCC_ABS8:
  case R_RISCC_ABS16:
  case R_RISCC_ABS32:
  case R_RISCC_LO8:
  case R_RISCC_HI8:
  case R_RISCC_CODE16:
  case R_RISCC_CODE_LO8:
  case R_RISCC_CODE_HI8:
    return R_ABS;
  case R_RISCC_TPOFF_LO8:
  case R_RISCC_TPOFF_HI8:
    return R_TPREL;
  case R_RISCC_PCREL8_WORD:
    return R_PC;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unknown relocation (" << type.v
             << ") against symbol " << &s;
    return R_NONE;
  }
}

void RISCC::scanSection(InputSectionBase &sec) {
  TargetInfo::scanSection(sec);

  // Non-ALLOC relocations describe the ELF/debug representation, where an
  // ordinary absolute byte address may legitimately name code. Only enforce
  // architectural code/data domains for allocated runtime sections.
  if (!(sec.flags & SHF_ALLOC))
    return;

  for (const Relocation &rel : sec.relocs()) {
    if (!rel.sym)
      continue;
    switch (rel.type) {
    case R_RISCC_ABS8:
    case R_RISCC_ABS16:
    case R_RISCC_ABS32:
    case R_RISCC_LO8:
    case R_RISCC_HI8:
      checkRelocationDomain(rel.type, *rel.sym, sec, rel.offset, false);
      break;
    case R_RISCC_CODE16:
    case R_RISCC_CODE_LO8:
    case R_RISCC_CODE_HI8:
    case R_RISCC_PCREL8_WORD:
      checkRelocationDomain(rel.type, *rel.sym, sec, rel.offset, true);
      break;
    case R_RISCC_TPOFF_LO8:
    case R_RISCC_TPOFF_HI8:
      if (!rel.sym->isTls())
        Err(ctx) << sec.getLocation(rel.offset) << ": relocation "
                 << rel.type << " against " << rel.sym
                 << " requires a TLS symbol";
      if (ctx.arg.shared)
        Err(ctx) << sec.getLocation(rel.offset) << ": relocation "
                 << rel.type << " cannot be used with -shared";
      break;
    default:
      break;
    }
  }
}

void RISCC::validateOutput() const {
  if (ctx.arg.relocatable)
    return;

  constexpr uint64_t codeLimit = 0x10000;
  for (const OutputSection *section : ctx.outputSections) {
    if ((section->flags & (SHF_ALLOC | SHF_EXECINSTR)) !=
            (SHF_ALLOC | SHF_EXECINSTR) ||
        section->size == 0)
      continue;

    if (section->addr & 1)
      Err(ctx) << "executable section " << section->name << " address 0x"
               << utohexstr(section->addr) << " is not 2-byte aligned";
    if (section->addr >= codeLimit ||
        section->size > codeLimit - section->addr)
      Err(ctx) << "executable section " << section->name << " range [0x"
               << utohexstr(section->addr) << ", 0x"
               << utohexstr(section->addr + section->size)
               << ") is outside the RISC-C code byte address range "
                  "[0x0, 0x10000)";
  }

  uint64_t entry;
  bool hasEntry = false;
  if (!ctx.arg.entry.empty()) {
    if (Symbol *symbol = ctx.symtab->find(ctx.arg.entry)) {
      if (symbol->isDefined()) {
        entry = symbol->getVA(ctx);
        hasEntry = true;
      }
    } else {
      hasEntry = to_integer(ctx.arg.entry, entry);
    }
  }
  if (hasEntry && ((entry & 1) || entry >= codeLimit))
    Err(ctx) << "entry point 0x" << utohexstr(entry)
             << " is not an aligned RISC-C code byte address in "
                "[0x0, 0x10000)";
}

bool RISCC::checkCodeAddress(uint8_t *loc, const Relocation &rel,
                             uint64_t val) const {
  if (val > 0xffff) {
    Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
             << " cannot encode tagged data-space or out-of-range address 0x"
             << utohexstr(val);
    return false;
  }
  checkAlignment(ctx, loc, val, 2, rel);
  return (val & 1) == 0;
}

void RISCC::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_RISCC_NONE:
    break;
  case R_RISCC_ABS8: {
    checkUInt(ctx, loc, val, 17, rel);
    uint64_t dataVal = val & 0xffff;
    checkUInt(ctx, loc, dataVal, 8, rel);
    *loc = dataVal;
    break;
  }
  case R_RISCC_ABS16:
    checkUInt(ctx, loc, val, 17, rel);
    write16le(loc, val & 0xffff);
    break;
  case R_RISCC_ABS32:
    checkUInt(ctx, loc, val, 17, rel);
    write32le(loc, val & 0xffff);
    break;
  case R_RISCC_LO8:
    checkUInt(ctx, loc, val, 17, rel);
    *loc = val & 0xff;
    break;
  case R_RISCC_HI8:
    checkUInt(ctx, loc, val, 17, rel);
    *loc = (val >> 8) & 0xff;
    break;
  case R_RISCC_CODE16:
    if (checkCodeAddress(loc, rel, val))
      write16le(loc, val >> 1);
    break;
  case R_RISCC_CODE_LO8:
    if (checkCodeAddress(loc, rel, val))
      *loc = (val >> 1) & 0xff;
    break;
  case R_RISCC_CODE_HI8:
    if (checkCodeAddress(loc, rel, val))
      *loc = (val >> 9) & 0xff;
    break;
  case R_RISCC_PCREL8_WORD: {
    uint64_t target = rel.sym ? rel.sym->getVA(ctx, rel.addend) : 0;
    if (!checkCodeAddress(loc, rel, target))
      break;
    checkAlignment(ctx, loc, val, 2, rel);
    int64_t wordOffset = (static_cast<int64_t>(val) - 2) / 2;
    checkInt(ctx, loc, wordOffset, 8, rel);
    *loc = wordOffset;
    break;
  }
  case R_RISCC_TPOFF_LO8:
    checkUInt(ctx, loc, val, 16, rel);
    *loc = val & 0xff;
    break;
  case R_RISCC_TPOFF_HI8:
    checkUInt(ctx, loc, val, 16, rel);
    *loc = (val >> 8) & 0xff;
    break;
  default:
    llvm_unreachable("unknown RISC-C relocation");
  }
}

void elf::setRISCCTargetInfo(Ctx &ctx) { ctx.target.reset(new RISCC(ctx)); }
