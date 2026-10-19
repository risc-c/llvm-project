//===- RISCC.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RISC-C is little-endian with one byte-addressed architectural address space.
// Code relocations are alignment-checked aliases of absolute relocations.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "InputSection.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "RelocScan.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Target.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TimeProfiler.h"
#include <cstring>
#include <numeric>

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
  void scanSection(InputSectionBase &sec, unsigned shard) override;
  template <class ELFT, class RelTy>
  void scanSectionImpl(InputSectionBase &sec, Relocs<RelTy> rels,
                       unsigned shard);
  void validateOutput() const override;
  bool relaxOnce(int pass) const override;
  void finalizeRelax(int passes) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;

private:
  struct CallRelaxation {
    InputSection *callSec;
    size_t callRelocIndex;
    size_t loadRelocIndex;
    InputSection *literalSec;
    size_t literalRelocIndex;
    Symbol *target;
    int64_t targetAddend;
    InputSection *targetSec;
    uint64_t targetOffset;
    bool tail;
    bool preserveLayout;
    bool valid = true;
    bool relaxed = false;
  };

  mutable SmallVector<CallRelaxation, 0> callRelaxations;
  mutable bool relaxationInitialized = false;

  struct LiteralWord {
    uint64_t offset; // Original offset in the input section.
    // marker, valueReloc and users index the input section's relocation list.
    size_t marker;
    size_t valueReloc = SIZE_MAX;
    uint32_t data;
    bool valid = true;
    SmallVector<size_t, 2> users;
    SmallVector<Defined *, 1> symbols;
  };
  struct LiteralRef {
    unsigned section, word;
    bool operator==(const LiteralRef &other) const {
      return section == other.section && word == other.word;
    }
  };
  struct LiteralSection {
    InputSection *sec;
    SmallVector<LiteralWord, 0> words;
    // (Relocation index, original target offset) for range checks.
    SmallVector<std::pair<size_t, uint64_t>, 0> edges;
    // Canonical word within this section, then its cross-section owner.
    SmallVector<unsigned, 0> representative;
    SmallVector<LiteralRef, 0> targets;
    // Slot positions use original offsets until finalizeRelax remaps them.
    SmallVector<uint64_t, 0> slots;
    // Original offsets of discarded words.
    SmallVector<uint64_t, 0> removed;
  };
  mutable SmallVector<LiteralSection, 0> literalSections;

  bool checkCodeAddress(uint8_t *loc, const Relocation &rel,
                        uint64_t val) const;
  uint32_t droppedBefore(const InputSection &sec, uint64_t offset) const;
  bool hasAddressMetadata() const;
  bool isInsideFunction(const InputSection &sec, uint64_t offset) const;
  void initRelaxation() const;
  void initLiteralRelaxation() const;
  void relaxLiterals() const;
  void shareLiteralsAcrossSections() const;
  void finalizeLiterals() const;
  bool relaxSection(InputSection &sec) const;
};
} // namespace

static bool isLDPC(uint16_t instruction) {
  return (instruction & 0xc700) == 0x8100;
}

static bool getInputSectionOffset(const Relocation &rel, InputSection *&sec,
                                  uint64_t &offset) {
  auto *def = dyn_cast_or_null<Defined>(rel.sym);
  sec = def ? dyn_cast_or_null<InputSection>(def->section) : nullptr;
  if (!sec)
    return false;
  int64_t value = int64_t(def->value) + rel.addend;
  if (value < 0 || uint64_t(value) > sec->size)
    return false;
  offset = value;
  return true;
}

static bool hasOverlappingRelocation(ArrayRef<Relocation> rels, uint64_t offset,
                                     const Relocation *first,
                                     const Relocation *second = nullptr) {
  for (const Relocation &rel : rels) {
    if (&rel == first || &rel == second || rel.type == R_RISCC_NONE ||
        rel.expr == R_RELAX_HINT)
      continue;
    unsigned size = 1;
    switch (rel.type) {
    case R_RISCC_ABS16:
    case R_RISCC_CODE16:
      size = 2;
      break;
    case R_RISCC_ABS32:
    case R_RISCC_TPOFF32:
    case R_RISCC_CALL_TARGET:
    case R_RISCC_JALL21:
      size = 4;
      break;
    default:
      break;
    }
    if (rel.offset < offset + 4 && rel.offset + size > offset)
      return true;
  }
  return false;
}

uint32_t RISCC::calcEFlags() const {
  if (ctx.objectFiles.empty())
    return 0;

  constexpr uint32_t knownMask =
      EF_RISCC_ABI_MASK | EF_RISCC_PROFILE_MASK | EF_RISCC_CONFIG_MASK;

  // Capability order is min < sys < full, although the numeric e_flags
  // encodings are not ordered that way. Nano is an incompatible profile:
  // Nano objects may link together, but never with a mainline object.
  unsigned outputRank = 0;
  bool sawNano = false;
  bool sawConfiguration = false;
  uint32_t configuration = 0;
  for (InputFile *file : ctx.objectFiles) {
    uint32_t flags = cast<ObjFile<ELF32LE>>(file)->getObj().getHeader().e_flags;
    if (uint32_t unknown = flags & ~knownMask)
      ErrAlways(ctx) << file << ": unsupported RISC-C ELF flags 0x"
                     << utohexstr(unknown);

    uint32_t abi = flags & EF_RISCC_ABI_MASK;
    if (abi != EF_RISCC_ABI_V0)
      ErrAlways(ctx) << file << ": unsupported RISC-C ABI flags 0x"
                     << utohexstr(abi) << " (expected ABI v0)";

    uint32_t config = flags & EF_RISCC_CONFIG_MASK;
    if ((config & EF_RISCC_RC32X) && !(config & EF_RISCC_RC32))
      ErrAlways(ctx) << file << ": RC32X requires the RC32 e_flags bit";
    if (config & EF_RISCC_RC32X)
      ErrAlways(ctx) << file << ": RC32X is not implemented";
    if (!sawConfiguration) {
      configuration = config;
      sawConfiguration = true;
    } else if (configuration != config)
      ErrAlways(ctx) << "cannot link RISC-C objects with different RC16, RC32, "
                        "or RC32X ABIs";

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
      if (config & EF_RISCC_RC32)
        ErrAlways(ctx) << file << ": RC32 has no Nano profile";
      sawNano = true;
      break;
    default:
      ErrAlways(ctx) << file << ": unsupported RISC-C ISA profile flags 0x"
                     << utohexstr(flags & EF_RISCC_PROFILE_MASK);
      break;
    }
    outputRank = std::max(outputRank, rank);
  }

  if (sawNano && outputRank)
    ErrAlways(ctx) << "cannot link incompatible RISC-C Nano and mainline "
                      "profiles";
  if (sawNano)
    return EF_RISCC_ABI_V0 | EF_RISCC_PROFILE_NANO;

  constexpr uint32_t profiles[] = {EF_RISCC_PROFILE_MIN, EF_RISCC_PROFILE_MIN,
                                   EF_RISCC_PROFILE_SYS, EF_RISCC_PROFILE_FULL};
  return EF_RISCC_ABI_V0 | profiles[outputRank] | configuration;
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
  case R_RISCC_JALL21:
  case R_RISCC_CALL_TARGET:
    return R_ABS;
  case R_RISCC_TPOFF_LO8:
  case R_RISCC_TPOFF_HI8:
  case R_RISCC_TPOFF32:
    return R_TPREL;
  case R_RISCC_PCREL8_WORD:
    return R_PC;
  case R_RISCC_RELAX_CALL:
  case R_RISCC_RELAX_TAIL:
  case R_RISCC_RELAX_LITERAL:
    return R_RELAX_HINT;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unknown relocation (" << type.v
             << ") against symbol " << &s;
    return R_NONE;
  }
}

template <class ELFT, class RelTy>
void RISCC::scanSectionImpl(InputSectionBase &sec, Relocs<RelTy> rels,
                            unsigned shard) {
  RelocScan rs(ctx, &sec, shard);
  sec.relocations.reserve(rels.size());
  for (auto it = rels.begin(); it != rels.end(); ++it) {
    auto type = it->getType(false);
    // NONE keeps its target alive during GC but has no field to relocate.
    if (type == R_RISCC_NONE)
      continue;
    if (type == R_RISCC_PCREL8_WORD &&
        (it->r_offset >= sec.size || sec.size - it->r_offset < 2)) {
      Err(ctx) << sec.getLocation(it->r_offset)
               << ": R_RISCC_PCREL8_WORD requires a complete instruction";
      continue;
    }
    rs.scan<ELFT, RelTy>(it, type, rs.getAddend<ELFT>(*it, type));
  }
}

void RISCC::scanSection(InputSectionBase &sec, unsigned shard) {
  elf::scanSection1<RISCC, ELF32LE>(*this, sec, shard);
  llvm::stable_sort(sec.relocs(),
                    [](const Relocation &lhs, const Relocation &rhs) {
                      return lhs.offset < rhs.offset;
                    });

  for (const Relocation &rel : sec.relocs()) {
    if (!rel.sym)
      continue;
    switch (rel.type) {
    case R_RISCC_TPOFF_LO8:
    case R_RISCC_TPOFF_HI8:
    case R_RISCC_TPOFF32:
      if (!rel.sym->isTls())
        Err(ctx) << sec.getLocation(rel.offset) << ": relocation " << rel.type
                 << " against " << rel.sym << " requires a TLS symbol";
      break;
    default:
      break;
    }
  }
}

void RISCC::validateOutput() const {
  if (ctx.arg.isPic) {
    ErrAlways(ctx) << "RISC-C ABI v0 does not support shared objects or "
                      "position-independent executables";
    return;
  }
  if (ctx.arg.relocatable)
    return;

  const bool isRC32 = calcEFlags() & EF_RISCC_RC32;
  const uint64_t codeLimit = isRC32 ? 0x1'0000'0000ULL : 0x10000;
  for (const OutputSection *section : ctx.outputSections) {
    if ((section->flags & (SHF_ALLOC | SHF_EXECINSTR)) !=
            (SHF_ALLOC | SHF_EXECINSTR) ||
        section->size == 0)
      continue;

    if (section->addr & 1)
      Err(ctx) << "executable section " << section->name << " address 0x"
               << utohexstr(section->addr) << " is not 2-byte aligned";
    if (section->addr >= codeLimit || section->size > codeLimit - section->addr)
      Err(ctx) << "executable section " << section->name << " range [0x"
               << utohexstr(section->addr) << ", 0x"
               << utohexstr(section->addr + section->size)
               << ") is outside the RISC-C code byte address range [0x0, 0x"
               << utohexstr(codeLimit) << ")";
  }

  uint64_t entry;
  bool hasEntry = false;
  if (!ctx.arg.entry.empty()) {
    if (Symbol *symbol = ctx.symtab->find(ctx.arg.entry)) {
      if (symbol->isDefined()) {
        if (const OutputSection *section = symbol->getOutputSection();
            section && !(section->flags & SHF_EXECINSTR))
          Err(ctx) << "entry symbol " << symbol
                   << " is defined in non-executable section " << section->name;
        entry = symbol->getVA(ctx);
        hasEntry = true;
      }
    } else {
      hasEntry = to_integer(ctx.arg.entry, entry);
    }
  }
  if (hasEntry && ((entry & 1) || entry >= codeLimit))
    Err(ctx) << "entry point 0x" << utohexstr(entry)
             << " is not an aligned RISC-C code byte address in [0x0, 0x"
             << utohexstr(codeLimit) << ")";
}

void RISCC::initRelaxation() const {
  initSymbolAnchors(ctx);
  relaxationInitialized = true;

  uint32_t flags = calcEFlags();
  uint32_t profile = flags & EF_RISCC_PROFILE_MASK;
  bool supportsRelaxation =
      (flags & EF_RISCC_RC32) &&
      (profile == EF_RISCC_PROFILE_SYS || profile == EF_RISCC_PROFILE_FULL);
  bool preserveFunctionLayout = hasAddressMetadata();

  SmallVector<InputSection *, 0> storage;
  for (OutputSection *osec : ctx.outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage)) {
      if (!sec->relaxAux)
        continue;
      MutableArrayRef<Relocation> rels = sec->relocs();
      for (auto [callIndex, call] : llvm::enumerate(rels)) {
        if (call.type != R_RISCC_RELAX_CALL && call.type != R_RISCC_RELAX_TAIL)
          continue;

        if (!supportsRelaxation) {
          Err(ctx) << sec->getLocation(call.offset)
                   << ": RISC-C call relaxation requires RC32 Sys or Full";
          continue;
        }

        auto *literal = dyn_cast_or_null<Defined>(call.sym);
        auto *literalSec =
            literal ? dyn_cast_or_null<InputSection>(literal->section)
                    : nullptr;
        if (!literalSec || !literalSec->relaxAux) {
          Err(ctx) << sec->getLocation(call.offset)
                   << ": relaxable RISC-C call does not reference a live "
                      "literal in an executable input section";
          continue;
        }
        int64_t literalOffset = int64_t(literal->value) + call.addend;
        if (literalOffset < 0 || literalSec->size < 4 ||
            uint64_t(literalOffset) > literalSec->size - 4 ||
            (literalOffset & 3)) {
          Err(ctx) << sec->getLocation(call.offset)
                   << ": relaxable RISC-C call references an invalid or "
                      "misaligned literal";
          continue;
        }

        if ((call.offset & 1) || sec->size < 4 || call.offset > sec->size - 4) {
          Err(ctx) << sec->getLocation(call.offset)
                   << ": relaxable RISC-C call has an invalid instruction "
                      "offset";
          continue;
        }

        ArrayRef<uint8_t> content = sec->content();
        uint16_t load = read16le(content.data() + call.offset);
        uint16_t transfer = read16le(content.data() + call.offset + 2);
        // Calls may use an object-private S link. Local far jumps may use
        // any non-SP scratch, but the load and transfer must agree on it.
        bool isTail = (transfer & 0x3800) == 0;
        unsigned scratch = (load >> 11) & 7;
        if (!isLDPC(load) || (transfer & ~0x3f00) != 0xc0f9 ||
            ((transfer >> 8) & 7) != scratch || scratch == 7 ||
            (!isTail && scratch != 0) ||
            isTail != (call.type == R_RISCC_RELAX_TAIL)) {
          Err(ctx) << sec->getLocation(call.offset)
                   << ": RISC-C relaxation marker is not attached to the "
                   << (call.type == R_RISCC_RELAX_TAIL
                           ? "LDPC rN/JALR s0,rN (r0..r6)"
                           : "LDPC r0/JALR s1..s7,r0")
                   << " instruction pair";
          continue;
        }

        size_t loadIndex = rels.size();
        for (auto [i, rel] : llvm::enumerate(rels)) {
          if (rel.type != R_RISCC_PCREL8_WORD || rel.offset != call.offset)
            continue;
          InputSection *loadTargetSec;
          uint64_t loadTargetOffset;
          if (!getInputSectionOffset(rel, loadTargetSec, loadTargetOffset) ||
              loadTargetSec != literalSec ||
              loadTargetOffset != uint64_t(literalOffset))
            continue;
          if (loadIndex != rels.size()) {
            loadIndex = rels.size();
            break;
          }
          loadIndex = i;
        }
        if (loadIndex == rels.size()) {
          Err(ctx) << sec->getLocation(call.offset)
                   << ": relaxable RISC-C call has no unique LDPC relocation "
                      "to its literal";
          continue;
        }

        MutableArrayRef<Relocation> literalRels = literalSec->relocs();
        size_t literalIndex = literalRels.size();
        for (auto [i, rel] : llvm::enumerate(literalRels))
          if (rel.type == R_RISCC_CALL_TARGET &&
              rel.offset == uint64_t(literalOffset)) {
            literalIndex = i;
            break;
          }
        if (literalIndex == literalRels.size()) {
          Err(ctx) << sec->getLocation(call.offset)
                   << ": relaxable RISC-C call literal has no "
                      "R_RISCC_CALL_TARGET relocation";
          continue;
        }
        Relocation &target = literalRels[literalIndex];
        if (hasOverlappingRelocation(rels, call.offset, &call,
                                     &rels[loadIndex]) ||
            hasOverlappingRelocation(literalRels, literalOffset, &target)) {
          Err(ctx) << sec->getLocation(call.offset)
                   << ": relaxable RISC-C call or literal overlaps another "
                      "relocation";
          continue;
        }
        if (!target.sym) {
          Err(ctx) << literalSec->getLocation(literalOffset)
                   << ": R_RISCC_CALL_TARGET has no target symbol";
          continue;
        }
        InputSection *targetSec = nullptr;
        uint64_t targetOffset = 0;
        if (auto *targetDef = dyn_cast_or_null<Defined>(target.sym);
            targetDef && targetDef->isSection()) {
          if (!getInputSectionOffset(target, targetSec, targetOffset)) {
            Err(ctx) << literalSec->getLocation(literalOffset)
                     << ": R_RISCC_CALL_TARGET has an invalid section offset";
            continue;
          }
        }
        callRelaxations.push_back(
            {sec, callIndex, loadIndex, literalSec, literalIndex, target.sym,
             target.addend, targetSec, targetOffset,
             call.type == R_RISCC_RELAX_TAIL,
             preserveFunctionLayout &&
                 isInsideFunction(*literalSec, literalOffset)});
      }
    }
  }

  // A literal may be owned by several marked transfers. Every live reference
  // must be one owner's marker or paired LDPC, at the start of the word; an
  // unmarked load or data reference prevents us from owning/deleting it.
  DenseMap<std::pair<InputSection *, size_t>, SmallVector<CallRelaxation *, 2>>
      owners;
  for (CallRelaxation &call : callRelaxations)
    owners[{call.literalSec, call.literalRelocIndex}].push_back(&call);
  for (auto &[key, group] : owners) {
    auto [literalSec, literalIndex] = key;
    uint64_t literalOffset = literalSec->relocs()[literalIndex].offset;
    unsigned references = 0;
    bool valid = true;
    for (InputSectionBase *base : ctx.inputSections) {
      auto *sec = dyn_cast<InputSection>(base);
      if (!sec || !sec->isLive())
        continue;
      for (auto [i, rel] : llvm::enumerate(sec->relocs())) {
        InputSection *targetSec;
        uint64_t targetOffset;
        if (!getInputSectionOffset(rel, targetSec, targetOffset) ||
            targetSec != literalSec || targetOffset < literalOffset ||
            targetOffset >= literalOffset + 4)
          continue;
        bool paired =
            targetOffset == literalOffset &&
            llvm::any_of(group, [&](const CallRelaxation *call) {
              return sec == call->callSec &&
                     (i == call->callRelocIndex || i == call->loadRelocIndex);
            });
        valid &= paired;
        ++references;
      }
    }
    for (CallRelaxation *call : group) {
      call->valid = valid && references == 2 * group.size();
      if (!call->valid)
        Err(ctx) << call->callSec->getLocation(
                        call->callSec->relocs()[call->callRelocIndex].offset)
                 << ": relaxable RISC-C call literal has unexpected references";
    }
  }
  initLiteralRelaxation();
}

bool RISCC::hasAddressMetadata() const {
  for (InputSectionBase *sec : ctx.inputSections) {
    if (!sec || !sec->isLive())
      continue;
    StringRef name = sec->name;
    if (name.starts_with(".debug_") || name.starts_with(".zdebug_") ||
        name == ".eh_frame")
      return true;
  }
  return false;
}

void RISCC::initLiteralRelaxation() const {
  if (!(calcEFlags() & EF_RISCC_RC32) || hasAddressMetadata())
    return;

  DenseMap<InputSection *, unsigned> sections;
  DenseMap<std::pair<InputSection *, uint64_t>, std::pair<unsigned, unsigned>>
      words;
  for (InputSectionBase *base : ctx.inputSections) {
    auto *sec = dyn_cast<InputSection>(base);
    if (!sec || !sec->isLive() || !sec->relaxAux || sec->addralign != 4)
      continue;
    LiteralSection pool{sec};
    for (auto [index, rel] : llvm::enumerate(sec->relocs())) {
      if (rel.type != R_RISCC_RELAX_LITERAL)
        continue;
      InputSection *targetSec;
      uint64_t targetOffset;
      if ((rel.offset & 3) || sec->size < 4 || rel.offset > sec->size - 4 ||
          !getInputSectionOffset(rel, targetSec, targetOffset) ||
          targetSec != sec || targetOffset != rel.offset) {
        Err(ctx) << sec->getLocation(rel.offset)
                 << ": RISC-C literal marker must name its own aligned word";
        continue;
      }
      auto [it, inserted] =
          words.try_emplace(std::make_pair(sec, rel.offset),
                            literalSections.size(), pool.words.size());
      if (!inserted) {
        Err(ctx) << sec->getLocation(rel.offset)
                 << ": duplicate RISC-C literal marker";
        continue;
      }
      LiteralWord word{rel.offset, index, SIZE_MAX,
                       read32le(sec->content().data() + rel.offset)};
      for (auto [i, field] : llvm::enumerate(sec->relocs())) {
        if (i == index || field.type == R_RISCC_NONE)
          continue;
        if (field.offset >= rel.offset && field.offset < rel.offset + 4) {
          if (field.offset != rel.offset || word.valueReloc != SIZE_MAX ||
              (field.type != R_RISCC_ABS32 && field.type != R_RISCC_TPOFF32))
            word.valid = false;
          else
            word.valueReloc = i;
        }
      }
      if (hasOverlappingRelocation(sec->relocs(), rel.offset, &rel,
                                   word.valueReloc == SIZE_MAX
                                       ? nullptr
                                       : &sec->relocs()[word.valueReloc]))
        word.valid = false;
      pool.words.push_back(std::move(word));
    }
    if (!pool.words.empty()) {
      sections[sec] = literalSections.size();
      literalSections.push_back(std::move(pool));
    }
  }

  if (literalSections.empty())
    return;

  // A word is private only when every reference is a same-section LDPC to
  // its beginning. No address escapes, partial-word references, or branches
  // into data may be redirected. Cross-section short references also make
  // shrinkage depend on output-section alignment/script decisions; leave
  // those layouts alone rather than predicting them here.
  bool crossSectionPCRel = false;
  for (InputSectionBase *base : ctx.inputSections) {
    auto *sec = dyn_cast<InputSection>(base);
    if (!sec || !sec->isLive())
      continue;
    for (auto [index, rel] : llvm::enumerate(sec->relocs())) {
      // A private label used as the base of address arithmetic is not just
      // a reference to that word's value. Keep its section/identity stable,
      // including when another word's marker is written as label + addend.
      if (auto *def = dyn_cast_or_null<Defined>(rel.sym);
          def && !def->isSection() && rel.addend != 0) {
        auto *baseSec = dyn_cast_or_null<InputSection>(def->section);
        if (auto it = words.find({baseSec, def->value & ~uint64_t(3)});
            it != words.end()) {
          auto [pool, word] = it->second;
          literalSections[pool].words[word].valid = false;
        }
      }
      InputSection *targetSec = nullptr;
      uint64_t targetOffset = 0;
      bool known = getInputSectionOffset(rel, targetSec, targetOffset);
      if (rel.type == R_RISCC_PCREL8_WORD) {
        if (!known || targetSec != sec)
          crossSectionPCRel = true;
        else if (auto it = sections.find(sec); it != sections.end())
          literalSections[it->second].edges.emplace_back(index, targetOffset);
      }
      auto it = words.find({targetSec, targetOffset & ~uint64_t(3)});
      if (!known || it == words.end())
        continue;
      auto [sectionIndex, wordIndex] = it->second;
      LiteralWord &word = literalSections[sectionIndex].words[wordIndex];
      if (sec == targetSec && index == word.marker)
        continue;
      if (sec != targetSec || targetOffset != word.offset ||
          rel.type != R_RISCC_PCREL8_WORD || sec->size < 2 ||
          rel.offset > sec->size - 2 || (rel.offset & 1) ||
          !isLDPC(read16le(sec->content().data() + rel.offset)))
        word.valid = false;
      else
        word.users.push_back(index);
    }
  }
  if (crossSectionPCRel) {
    literalSections.clear();
    return;
  }
  for (InputFile *file : ctx.objectFiles)
    for (Symbol *sym : file->getSymbols()) {
      auto *def = dyn_cast<Defined>(sym);
      if (!def || def->isSection())
        continue;
      auto *sec = dyn_cast_or_null<InputSection>(def->section);
      auto it = words.find({sec, def->value & ~uint64_t(3)});
      if (it == words.end())
        continue;
      auto [sectionIndex, wordIndex] = it->second;
      LiteralWord &word = literalSections[sectionIndex].words[wordIndex];
      if (!def->isLocal() || def->isFunc() || def->size ||
          def->value != word.offset)
        word.valid = false;
      else
        word.symbols.push_back(def);
    }
  for (LiteralSection &pool : literalSections)
    llvm::erase_if(pool.words, [](const LiteralWord &word) {
      return !word.valid || word.users.empty();
    });
}

void RISCC::relaxLiterals() const {
  for (LiteralSection &pool : literalSections) {
    ArrayRef<Relocation> rels = pool.sec->relocs();
    const unsigned count = pool.words.size();
    // Rebuild from the original coordinates on every pass: linker-script
    // address changes may undo a call relaxation and restore its target word.
    pool.representative.resize(count);
    std::iota(pool.representative.begin(), pool.representative.end(), 0);
    pool.slots.clear();
    pool.removed.clear();
    DenseMap<uint64_t, unsigned> wordAt;
    for (auto [i, word] : llvm::enumerate(pool.words)) {
      pool.slots.push_back(word.offset);
      wordAt[word.offset] = i;
    }
    SmallVector<uint64_t, 0> removed;
    SmallDenseSet<size_t, 8> relaxedLoads;
    for (const CallRelaxation &call : callRelaxations) {
      if (!call.valid || !call.relaxed)
        continue;
      if (call.callSec == pool.sec)
        relaxedLoads.insert(call.loadRelocIndex);
      if (call.literalSec == pool.sec && !call.preserveLayout &&
          llvm::none_of(callRelaxations, [&](const CallRelaxation &other) {
            return other.valid && other.literalSec == call.literalSec &&
                   other.literalRelocIndex == call.literalRelocIndex &&
                   (!other.relaxed || other.preserveLayout);
          }))
        removed.push_back(rels[call.literalRelocIndex].offset);
    }
    llvm::sort(removed);
    removed.erase(std::unique(removed.begin(), removed.end()), removed.end());
    auto sameValue = [&](unsigned a, unsigned b) {
      const LiteralWord &lhs = pool.words[a], &rhs = pool.words[b];
      if (lhs.valueReloc == SIZE_MAX || rhs.valueReloc == SIZE_MAX)
        return lhs.valueReloc == rhs.valueReloc && lhs.data == rhs.data;
      const Relocation &x = rels[lhs.valueReloc], &y = rels[rhs.valueReloc];
      return x.type == y.type && x.expr == y.expr && x.sym == y.sym &&
             x.addend == y.addend;
    };
    while (true) {
      bool accepted = false;
      for (unsigned victim = 0; victim != count && !accepted; ++victim) {
        if (pool.representative[victim] != victim)
          continue;
        for (unsigned keep = 0; keep != count; ++keep) {
          if (keep == victim || pool.representative[keep] != keep ||
              !sameValue(victim, keep))
            continue;
          auto representatives = pool.representative;
          for (unsigned &r : representatives)
            if (r == victim)
              r = keep;
          auto deletions = removed;
          deletions.push_back(pool.slots[victim]);
          llvm::sort(deletions);
          auto position = [&](uint64_t offset) -> int64_t {
            return int64_t(offset) - 4 * (llvm::lower_bound(deletions, offset) -
                                          deletions.begin());
          };
          SmallVector<std::pair<int64_t, int64_t>, 0> intervals(
              count, {INT64_MIN, INT64_MAX});
          // Each surviving value needs a slot in the intersection of all
          // its users' LDPC ranges, including users of the deleted copy.
          for (unsigned i = 0; i != count; ++i)
            for (size_t user : pool.words[i].users) {
              int64_t offset = position(rels[user].offset);
              auto &range = intervals[representatives[i]];
              range.first = std::max(range.first, offset - 254);
              range.second = std::min(range.second, offset + 256);
            }
          if (intervals[keep].first > intervals[keep].second)
            continue;
          SmallVector<uint64_t, 0> available;
          SmallVector<unsigned, 0> pending;
          for (unsigned i = 0; i != count; ++i)
            if (representatives[i] == i) {
              available.push_back(pool.slots[i]);
              pending.push_back(i);
            }
          llvm::sort(available);
          auto slots = pool.slots;
          // Assign existing slots from left to right, taking the reachable
          // value whose range ends first. This also permits exchanges when
          // neither duplicate's original slot can serve all of its users.
          // No new pool, alignment padding, or skip branch is introduced.
          for (uint64_t slot : available) {
            int64_t at = position(slot);
            auto best = pending.end();
            for (auto it = pending.begin(); it != pending.end(); ++it)
              if (intervals[*it].first <= at &&
                  (best == pending.end() ||
                   intervals[*it].second < intervals[*best].second ||
                   (intervals[*it].second == intervals[*best].second &&
                    pool.slots[*it] == slot)))
                best = it;
            if (best == pending.end() || intervals[*best].second < at)
              break;
            slots[*best] = slot;
            pending.erase(best);
          }
          if (!pending.empty())
            continue;
          bool fits = llvm::all_of(pool.edges, [&](auto edge) {
            auto [index, target] = edge;
            if (relaxedLoads.contains(index))
              return true;
            if (pool.sec->size < 2 || rels[index].offset > pool.sec->size - 2 ||
                (rels[index].offset & 1))
              return false;
            auto it = wordAt.find(target);
            int64_t to = position(it == wordAt.end()
                                      ? target
                                      : slots[representatives[it->second]]);
            int64_t distance = to - position(rels[index].offset) - 2;
            bool load = isLDPC(
                read16le(pool.sec->content().data() + rels[index].offset));
            return !(distance & 1) && isInt<8>(distance / 2) &&
                   (!load || !(to & 3));
          });
          if (!fits)
            continue;
          pool.removed.push_back(pool.slots[victim]);
          pool.representative = std::move(representatives);
          pool.slots = std::move(slots);
          removed = std::move(deletions);
          accepted = true;
          break;
        }
      }
      if (!accepted)
        break;
    }
  }
  shareLiteralsAcrossSections();
}

void RISCC::shareLiteralsAcrossSections() const {
  DenseMap<InputSection *, unsigned> poolFor;
  for (auto [i, pool] : llvm::enumerate(literalSections)) {
    pool.targets.clear();
    for (unsigned representative : pool.representative)
      pool.targets.push_back({unsigned(i), representative});
    if (!pool.words.empty())
      poolFor[pool.sec] = i;
  }
  if (poolFor.size() < 2)
    return;

  DenseMap<InputSection *, SmallVector<uint64_t, 0>> callDeletions;
  DenseSet<std::pair<InputSection *, size_t>> relaxedLoads;
  for (const CallRelaxation &call : callRelaxations) {
    if (!call.valid || !call.relaxed)
      continue;
    relaxedLoads.insert({call.callSec, call.loadRelocIndex});
    if (!call.preserveLayout &&
        llvm::none_of(callRelaxations, [&](const CallRelaxation &other) {
          return other.valid && other.literalSec == call.literalSec &&
                 other.literalRelocIndex == call.literalRelocIndex &&
                 (!other.relaxed || other.preserveLayout);
        }))
      callDeletions[call.literalSec].push_back(
          call.literalSec->relocs()[call.literalRelocIndex].offset);
  }

  // A region starts at a word-aligned pool section and contains only regular
  // sections with alignment <= 4 in one input-section description. Deleting
  // whole words preserves all padding within it, so relative positions can
  // be calculated exactly without guessing future VAs. Do not span script
  // commands, synthetic sections, or stronger alignment. Rebuild membership
  // every pass because memory-region spilling can move input sections.
  auto shareRegion = [&](ArrayRef<InputSection *> sections) {
    DenseMap<InputSection *, uint64_t> starts;
    SmallVector<unsigned, 0> pools;
    SmallVector<LiteralRef, 0> candidates;
    DenseMap<std::pair<InputSection *, uint64_t>, LiteralRef> wordAt;
    SmallVector<uint64_t, 0> removed;
    uint64_t end = 0;
    for (InputSection *sec : sections) {
      uint64_t start = alignTo(end, sec->addralign);
      starts[sec] = start;
      end = start + sec->size;
      if (auto it = callDeletions.find(sec); it != callDeletions.end())
        for (uint64_t offset : it->second)
          removed.push_back(start + offset);
      auto it = poolFor.find(sec);
      if (it == poolFor.end())
        continue;
      unsigned index = it->second;
      pools.push_back(index);
      const LiteralSection &pool = literalSections[index];
      for (uint64_t offset : pool.removed)
        removed.push_back(start + offset);
      for (auto [i, word] : llvm::enumerate(pool.words)) {
        LiteralRef ref{index, unsigned(i)};
        wordAt[{sec, word.offset}] = ref;
        if (pool.targets[i] == ref)
          candidates.push_back(ref);
      }
    }
    if (pools.size() < 2)
      return;
    llvm::sort(removed);
    removed.erase(std::unique(removed.begin(), removed.end()), removed.end());
    auto slotOf = [&](LiteralRef ref) {
      const LiteralSection &pool = literalSections[ref.section];
      return starts.lookup(pool.sec) + pool.slots[ref.word];
    };
    llvm::sort(candidates, [&](LiteralRef a, LiteralRef b) {
      return slotOf(a) < slotOf(b);
    });
    auto sameValue = [&](LiteralRef a, LiteralRef b) {
      const LiteralSection &x = literalSections[a.section];
      const LiteralSection &y = literalSections[b.section];
      const LiteralWord &lhs = x.words[a.word], &rhs = y.words[b.word];
      if (lhs.valueReloc == SIZE_MAX || rhs.valueReloc == SIZE_MAX)
        return lhs.valueReloc == rhs.valueReloc && lhs.data == rhs.data;
      const Relocation &r = x.sec->relocs()[lhs.valueReloc];
      const Relocation &s = y.sec->relocs()[rhs.valueReloc];
      return r.type == s.type && r.expr == s.expr && r.sym == s.sym &&
             r.addend == s.addend;
    };
    auto position = [](ArrayRef<uint64_t> deletions, uint64_t offset) {
      return int64_t(offset) -
             4 * (llvm::lower_bound(deletions, offset) - deletions.begin());
    };
    while (true) {
      bool accepted = false;
      for (LiteralRef victim : candidates) {
        LiteralSection &victimPool = literalSections[victim.section];
        if (!(victimPool.targets[victim.word] == victim))
          continue;
        int64_t at = position(removed, slotOf(victim));
        // Both old and replacement words must reach a common user. The
        // extra four bytes allow for the proposed deletion. Limit the search
        // to this neighborhood even for very large output sections.
        auto first = llvm::lower_bound(
            candidates, at - 516, [&](LiteralRef ref, int64_t bound) {
              return position(removed, slotOf(ref)) < bound;
            });
        for (LiteralRef keep : make_range(first, candidates.end())) {
          if (position(removed, slotOf(keep)) > at + 516)
            break;
          const LiteralSection &keepPool = literalSections[keep.section];
          if (keep.section == victim.section ||
              !(keepPool.targets[keep.word] == keep) ||
              !sameValue(victim, keep))
            continue;
          auto deletions = removed;
          deletions.insert(llvm::lower_bound(deletions, slotOf(victim)),
                           slotOf(victim));
          bool fits = llvm::all_of(pools, [&](unsigned index) {
            const LiteralSection &pool = literalSections[index];
            ArrayRef<Relocation> rels = pool.sec->relocs();
            uint64_t start = starts.lookup(pool.sec);
            return llvm::all_of(pool.edges, [&](auto edge) {
              auto [i, target] = edge;
              if (relaxedLoads.contains({pool.sec, i}))
                return true;
              if (pool.sec->size < 2 || rels[i].offset > pool.sec->size - 2 ||
                  (rels[i].offset & 1))
                return false;
              uint64_t destination = start + target;
              if (auto it = wordAt.find({pool.sec, target});
                  it != wordAt.end()) {
                LiteralRef ref = pool.targets[it->second.word];
                destination = slotOf(ref == victim ? keep : ref);
              }
              int64_t to = position(deletions, destination);
              int64_t distance =
                  to - position(deletions, start + rels[i].offset) - 2;
              bool load =
                  isLDPC(read16le(pool.sec->content().data() + rels[i].offset));
              return !(distance & 1) && isInt<8>(distance / 2) &&
                     (!load || !(to & 3));
            });
          });
          if (!fits)
            continue;
          victimPool.removed.push_back(victimPool.slots[victim.word]);
          for (unsigned index : pools)
            for (LiteralRef &ref : literalSections[index].targets)
              if (ref == victim)
                ref = keep;
          removed = std::move(deletions);
          accepted = true;
          break;
        }
        if (accepted)
          break;
      }
      if (!accepted)
        break;
    }
  };

  for (OutputSection *osec : ctx.outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (SectionCommand *command : osec->commands) {
      auto *description = dyn_cast<InputSectionDescription>(command);
      if (!description)
        continue;
      SmallVector<InputSection *, 0> region;
      for (InputSection *sec : description->sections) {
        if (sec->kind() != SectionBase::Regular || sec->addralign > 4) {
          shareRegion(region);
          region.clear();
        } else if (!region.empty() || poolFor.contains(sec)) {
          region.push_back(sec);
        }
      }
      shareRegion(region);
    }
  }
}

void RISCC::finalizeLiterals() const {
  // Move private aliases before rewriting any loads: a canonical word may
  // belong to a later input section, and its marker may use a local label.
  for (const LiteralSection &pool : literalSections)
    for (auto [i, word] : llvm::enumerate(pool.words)) {
      LiteralRef target = pool.targets[i];
      const LiteralSection &owner = literalSections[target.section];
      for (Defined *symbol : word.symbols) {
        symbol->section = owner.sec;
        symbol->value = owner.slots[target.word];
      }
    }
  for (const LiteralSection &pool : literalSections) {
    auto rels = pool.sec->relocs();
    auto *buf = const_cast<uint8_t *>(pool.sec->content().data());
    for (auto [i, word] : llvm::enumerate(pool.words)) {
      LiteralRef target = pool.targets[i];
      const LiteralSection &owner = literalSections[target.section];
      uint64_t slot = owner.slots[target.word];
      Relocation &marker = rels[word.marker];
      for (size_t user : word.users) {
        Relocation &load = rels[user];
        // Preserve same-section relocation spellings, but a cross-section
        // reference must use a symbol belonging to the surviving word.
        load.sym =
            owner.sec == pool.sec
                ? marker.sym
                : owner.sec->relocs()[owner.words[target.word].marker].sym;
        load.addend = int64_t(slot) - cast<Defined>(load.sym)->value;
      }
      marker.type = R_RISCC_NONE;
      marker.addend = 0;
      if (owner.sec == pool.sec && target.word == i) {
        write32le(buf + slot, word.data);
        if (word.valueReloc != SIZE_MAX)
          rels[word.valueReloc].offset = slot;
      } else if (word.valueReloc != SIZE_MAX) {
        Relocation &value = rels[word.valueReloc];
        value.type = R_RISCC_NONE;
        value.expr = R_RELAX_HINT;
        value.addend = 0;
      }
    }
  }
}

bool RISCC::isInsideFunction(const InputSection &sec, uint64_t offset) const {
  for (InputFile *file : ctx.objectFiles)
    for (Symbol *sym : file->getSymbols()) {
      auto *def = dyn_cast<Defined>(sym);
      if (!def || !def->isFunc() || def->section != &sec || !def->size)
        continue;
      if (def->value <= offset && offset < def->value + def->size)
        return true;
    }
  return false;
}

bool RISCC::relaxSection(InputSection &sec) const {
  MutableArrayRef<Relocation> rels = sec.relocs();
  if (rels.empty() || !sec.relaxAux)
    return false;

  RelaxAux &aux = *sec.relaxAux;
  std::fill_n(aux.relocTypes.get(), rels.size(), R_RISCC_NONE);
  aux.writes.clear();
  for (const CallRelaxation &call : callRelaxations)
    if (call.valid && call.relaxed && call.callSec == &sec)
      aux.relocTypes[call.callRelocIndex] = R_RISCC_JALL21;

  auto deleteLiteral = [&](size_t index) {
    for (const LiteralSection &pool : literalSections)
      if (pool.sec == &sec)
        for (const LiteralWord &word : pool.words)
          if (word.marker == index &&
              llvm::is_contained(pool.removed, word.offset))
            return true;
    bool referenced = false;
    for (const CallRelaxation &call : callRelaxations) {
      if (!call.valid)
        continue;
      if (call.literalSec != &sec || call.literalRelocIndex != index)
        continue;
      referenced = true;
      if (!call.relaxed || call.preserveLayout)
        return false;
    }
    return referenced;
  };

  bool changed = false;
  ArrayRef<SymbolAnchor> anchors = aux.anchors;
  uint64_t delta = 0;
  for (auto [i, rel] : llvm::enumerate(rels)) {
    uint32_t &current = aux.relocDeltas[i];
    uint32_t remove = deleteLiteral(i) ? 4 : 0;

    for (; !anchors.empty() && anchors.front().offset <= rel.offset;
         anchors = anchors.drop_front()) {
      const SymbolAnchor &anchor = anchors.front();
      if (anchor.end)
        anchor.d->size = anchor.offset - delta - anchor.d->value;
      else
        anchor.d->value = anchor.offset - delta;
    }

    delta += remove;
    if (current != delta) {
      current = delta;
      changed = true;
    }
  }
  for (const SymbolAnchor &anchor : anchors) {
    if (anchor.end)
      anchor.d->size = anchor.offset - delta - anchor.d->value;
    else
      anchor.d->value = anchor.offset - delta;
  }
  if (!isUInt<32>(delta))
    Err(ctx) << "RISC-C section size decrease is too large: " << delta;
  sec.bytesDropped = delta;
  return changed;
}

uint32_t RISCC::droppedBefore(const InputSection &sec, uint64_t offset) const {
  if (!sec.relaxAux || !sec.relaxAux->relocDeltas)
    return 0;
  uint32_t delta = 0;
  for (auto [i, rel] : llvm::enumerate(sec.relocs())) {
    if (rel.offset >= offset)
      break;
    delta = sec.relaxAux->relocDeltas[i];
  }
  return delta;
}

bool RISCC::relaxOnce(int pass) const {
  if (!ctx.arg.relax)
    return false;
  llvm::TimeTraceScope timeScope("RISC-C relaxOnce");
  if (pass == 0)
    initRelaxation();

  for (CallRelaxation &call : callRelaxations) {
    if (!call.valid) {
      call.relaxed = false;
      continue;
    }
    uint64_t target =
        call.targetSec ? call.targetSec->getVA() + call.targetOffset -
                             droppedBefore(*call.targetSec, call.targetOffset)
                       : call.target->getVA(ctx, call.targetAddend);
    bool candidate = !(target & 1) && target <= 0x1fffff;
    // After four passes, only undo relaxations so linker-script expressions
    // cannot make section sizes oscillate.
    if (pass < 4 || call.relaxed)
      call.relaxed = candidate;
  }
  relaxLiterals();

  SmallVector<InputSection *, 0> storage;
  bool changed = false;
  for (OutputSection *osec : ctx.outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage))
      if (sec->relaxAux)
        changed |= relaxSection(*sec);
  }
  return changed;
}

void RISCC::finalizeRelax(int passes) const {
  if (!relaxationInitialized)
    return;
  llvm::TimeTraceScope timeScope("Finalize RISC-C relaxation");
  Log(ctx) << "RISC-C relaxation passes: " << passes;

  // Convert original word-slot coordinates while relocation offsets still
  // describe the input section. Later copying mutates those offsets in place.
  for (LiteralSection &pool : literalSections)
    for (uint64_t &slot : pool.slots)
      slot -= droppedBefore(*pool.sec, slot);

  SmallVector<int64_t, 0> finalTargetAddends;
  finalTargetAddends.reserve(callRelaxations.size());
  for (const CallRelaxation &call : callRelaxations) {
    if (!call.targetSec) {
      finalTargetAddends.push_back(call.targetAddend);
      continue;
    }
    finalTargetAddends.push_back(
        call.targetOffset - droppedBefore(*call.targetSec, call.targetOffset));
  }

  // Temporary labels are conventionally represented as section-symbol plus
  // addend relocations. Adjust those addends for all references to a shrunken
  // executable input section, including compact branches, long-branch
  // literals, data pointers, and unrelaxed call-target literals.
  for (InputSectionBase *base : ctx.inputSections) {
    auto *sec = dyn_cast<InputSection>(base);
    if (!sec || !sec->isLive())
      continue;
    for (Relocation &rel : sec->relocs()) {
      auto *target = dyn_cast_or_null<Defined>(rel.sym);
      if (!target || !target->isSection())
        continue;
      auto *targetSec = dyn_cast_or_null<InputSection>(target->section);
      if (!targetSec || !targetSec->relaxAux)
        continue;
      int64_t offset = int64_t(target->value) + rel.addend;
      if (offset < 0)
        continue;
      rel.addend -= droppedBefore(*targetSec, offset);
    }
  }

  SmallVector<InputSection *, 0> storage;
  for (OutputSection *osec : ctx.outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage)) {
      if (!sec->relaxAux || sec->relocs().empty())
        continue;
      RelaxAux &aux = *sec->relaxAux;
      MutableArrayRef<Relocation> rels = sec->relocs();
      ArrayRef<uint8_t> old = sec->content();
      size_t newSize = old.size() - aux.relocDeltas[rels.size() - 1];
      uint8_t *out = ctx.bAlloc.Allocate<uint8_t>(newSize);
      uint8_t *p = out;
      uint64_t offset = 0;
      uint32_t delta = 0;

      for (size_t i = 0; i != rels.size(); ++i) {
        uint32_t remove = aux.relocDeltas[i] - delta;
        delta = aux.relocDeltas[i];
        bool rewrite = aux.relocTypes[i] == R_RISCC_JALL21;
        if (!remove && !rewrite)
          continue;

        const Relocation &rel = rels[i];
        size_t copy = rel.offset - offset;
        memcpy(p, old.data() + offset, copy);
        p += copy;

        uint32_t keep = 0;
        if (rewrite) {
          uint16_t link = read16le(old.data() + rel.offset + 2) & 0x3800;
          write32le(p, 0x00000034 | link);
          keep = 4;
          p += keep;
        }
        offset = rel.offset + keep + remove;
      }
      memcpy(p, old.data() + offset, old.size() - offset);
      sec->content_ = out;
      sec->size = newSize;
      sec->bytesDropped = 0;

      delta = 0;
      for (size_t i = 0; i != rels.size();) {
        uint64_t originalOffset = rels[i].offset;
        do {
          rels[i].offset -= delta;
        } while (++i != rels.size() && rels[i].offset == originalOffset);
        delta = aux.relocDeltas[i - 1];
      }
    }
  }

  for (auto [index, relax] : llvm::enumerate(callRelaxations)) {
    if (!relax.relaxed)
      continue;
    Relocation &call = relax.callSec->relocs()[relax.callRelocIndex];

    // The relaxation marker shares its offset with the LDPC it replaces.
    // Local RC32 LDPC references remain relocatable so other literal loads can
    // be adjusted when a pool word is removed; discard this particular fixup
    // before turning the instruction into JALL/JMPL.
    Relocation &load = relax.callSec->relocs()[relax.loadRelocIndex];
    load.expr = R_RELAX_HINT;
    load.type = R_RISCC_NONE;
    load.addend = 0;

    call.expr = R_ABS;
    call.type = R_RISCC_JALL21;
    call.sym = relax.target;
    call.addend = finalTargetAddends[index];

    Relocation &literal = relax.literalSec->relocs()[relax.literalRelocIndex];
    literal.expr = R_RELAX_HINT;
    literal.type = R_RISCC_NONE;
    literal.addend = 0;
  }
  finalizeLiterals();

  // A nearby tail transfer needs no link write. Keep its unused second
  // halfword: shrinking by two would disturb RC32 literal alignment and
  // assembler-resolved RC16 branches. All buffers below were copied above.
  const uint64_t codeLimit =
      calcEFlags() & EF_RISCC_RC32 ? 0x1'0000'0000ULL : 0x10000;
  for (OutputSection *osec : ctx.outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage)) {
      if (!sec->relaxAux || sec->relocs().empty())
        continue;
      auto *buf = const_cast<uint8_t *>(sec->content().data());
      for (Relocation &rel : sec->relocs()) {
        uint64_t offset = rel.offset;
        if (rel.type == R_RISCC_CODE16) {
          if (offset < 2)
            continue;
          offset -= 2;
        } else if (rel.type != R_RISCC_JALL21) {
          continue;
        }
        if (!rel.sym || offset > sec->size || sec->size - offset < 4 ||
            (offset & 1) || read16le(buf + offset) != 0x0034 ||
            hasOverlappingRelocation(sec->relocs(), offset, &rel))
          continue;
        uint64_t target = rel.sym->getVA(ctx, rel.addend);
        int64_t distance = target - (sec->getVA(offset) + 2);
        if ((target & 1) || target >= codeLimit || !isInt<9>(distance))
          continue;
        write16le(buf + offset, 0xa700); // JMP8; operand relocated below.
        write16le(buf + offset + 2, target); // Retain the old operand halfword.
        rel.offset = offset;
        rel.type = R_RISCC_PCREL8_WORD;
        rel.expr = R_PC;
      }
    }
  }

  // Slot exchanges move value relocations in either direction. Restore their
  // ordering for --emit-relocs after all relaxation indices have been consumed.
  for (const LiteralSection &pool : literalSections)
    if (!pool.removed.empty())
      llvm::stable_sort(pool.sec->relocs(),
                        [](const Relocation &a, const Relocation &b) {
                          return a.offset < b.offset;
                        });
}

bool RISCC::checkCodeAddress(uint8_t *loc, const Relocation &rel,
                             uint64_t val) const {
  if (val > 0xffff) {
    Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
             << " cannot encode out-of-range byte address 0x" << utohexstr(val);
    return false;
  }
  checkAlignment(ctx, loc, val, 2, rel);
  return (val & 1) == 0;
}

void RISCC::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_RISCC_NONE:
    break;
  case R_RISCC_ABS8:
    checkUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_RISCC_ABS16:
    checkUInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_RISCC_ABS32:
  case R_RISCC_TPOFF32:
  case R_RISCC_CALL_TARGET:
    checkIntUInt(ctx, loc, val, 32, rel);
    if (rel.type == R_RISCC_CALL_TARGET)
      checkAlignment(ctx, loc, val, 2, rel);
    write32le(loc, val);
    break;
  case R_RISCC_LO8:
  case R_RISCC_TPOFF_LO8:
    checkUInt(ctx, loc, val, 16, rel);
    *loc = val & 0xff;
    break;
  case R_RISCC_HI8:
  case R_RISCC_TPOFF_HI8:
    checkUInt(ctx, loc, val, 16, rel);
    *loc = (val >> 8) & 0xff;
    break;
  case R_RISCC_CODE16:
    if (checkCodeAddress(loc, rel, val))
      write16le(loc, val);
    break;
  case R_RISCC_CODE_LO8:
    if (checkCodeAddress(loc, rel, val))
      *loc = val & 0xff;
    break;
  case R_RISCC_CODE_HI8:
    if (checkCodeAddress(loc, rel, val))
      *loc = (val >> 8) & 0xff;
    break;
  case R_RISCC_JALL21: {
    checkAlignment(ctx, loc, val, 2, rel);
    if (val > 0x1fffff) {
      checkUInt(ctx, loc, val, 21, rel);
      break;
    }
    uint16_t head = read16le(loc);
    write16le(loc, (head & ~0x07c0) | ((val >> 16) & 0x1f) << 6);
    write16le(loc + 2, val);
    break;
  }
  case R_RISCC_PCREL8_WORD: {
    if ((ctx.arg.eflags & EF_RISCC_RC32) && isLDPC(read16le(loc)))
      checkAlignment(ctx, loc, rel.sym->getVA(ctx, rel.addend), 4, rel);
    checkAlignment(ctx, loc, val, 2, rel);
    int64_t wordOffset = (static_cast<int64_t>(val) - 2) / 2;
    checkInt(ctx, loc, wordOffset, 8, rel);
    uint8_t encoded = static_cast<uint8_t>(wordOffset);
    *loc = static_cast<uint8_t>((encoded << 1) | (encoded >> 7));
    break;
  }
  case R_RISCC_RELAX_CALL:
  case R_RISCC_RELAX_TAIL:
  case R_RISCC_RELAX_LITERAL:
    break;
  default:
    llvm_unreachable("unknown RISC-C relocation");
  }
}

void elf::setRISCCTargetInfo(Ctx &ctx) { ctx.target.reset(new RISCC(ctx)); }
