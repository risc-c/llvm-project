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
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TimeProfiler.h"
#include <cstring>

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

  bool checkCodeAddress(uint8_t *loc, const Relocation &rel,
                        uint64_t val) const;
  uint32_t droppedBefore(const InputSection &sec, uint64_t offset) const;
  bool hasAddressMetadata() const;
  bool isInsideFunction(const InputSection &sec, uint64_t offset) const;
  void initRelaxation() const;
  bool relaxSection(InputSection &sec) const;
};
} // namespace

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

uint32_t RISCC::calcEFlags() const {
  if (ctx.objectFiles.empty())
    return 0;

  constexpr uint32_t knownMask = EF_RISCC_ABI_MASK | EF_RISCC_PROFILE_MASK |
                                 EF_RISCC_CONFIG_MASK;

  // Capability order is min < sys < full, although the numeric e_flags
  // encodings are not ordered that way. Nano is an incompatible profile:
  // Nano objects may link together, but never with a mainline object.
  unsigned outputRank = 0;
  bool sawNano = false;
  bool sawConfiguration = false;
  uint32_t configuration = 0;
  for (InputFile *file : ctx.objectFiles) {
    uint32_t flags =
        cast<ObjFile<ELF32LE>>(file)->getObj().getHeader().e_flags;
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

  constexpr uint32_t profiles[] = {EF_RISCC_PROFILE_MIN,
                                   EF_RISCC_PROFILE_MIN,
                                   EF_RISCC_PROFILE_SYS,
                                   EF_RISCC_PROFILE_FULL};
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
    return R_RELAX_HINT;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unknown relocation (" << type.v
             << ") against symbol " << &s;
    return R_NONE;
  }
}

void RISCC::scanSection(InputSectionBase &sec) {
  TargetInfo::scanSection(sec);

  for (const Relocation &rel : sec.relocs()) {
    if (!rel.sym)
      continue;
    switch (rel.type) {
    case R_RISCC_TPOFF_LO8:
    case R_RISCC_TPOFF_HI8:
    case R_RISCC_TPOFF32:
      if (!rel.sym->isTls())
        Err(ctx) << sec.getLocation(rel.offset) << ": relocation "
                 << rel.type << " against " << rel.sym
                 << " requires a TLS symbol";
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
        if (const OutputSection *section = symbol->getOutputSection();
            section && !(section->flags & SHF_EXECINSTR))
          Err(ctx) << "entry symbol " << symbol
                   << " is defined in non-executable section "
                   << section->name;
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
        if (call.type != R_RISCC_RELAX_CALL &&
            call.type != R_RISCC_RELAX_TAIL)
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

        if ((call.offset & 1) || sec->size < 4 ||
            call.offset > sec->size - 4) {
          Err(ctx) << sec->getLocation(call.offset)
                   << ": relaxable RISC-C call has an invalid instruction "
                      "offset";
          continue;
        }

        ArrayRef<uint8_t> content = sec->content();
        uint16_t load = read16le(content.data() + call.offset);
        uint16_t transfer = read16le(content.data() + call.offset + 2);
        uint16_t expectedTransfer =
            call.type == R_RISCC_RELAX_TAIL ? 0xc0f9 : 0xf8f9;
        if ((load & 0xff00) != 0x8100 || transfer != expectedTransfer) {
          Err(ctx) << sec->getLocation(call.offset)
                   << ": RISC-C relaxation marker is not attached to the "
                   << (call.type == R_RISCC_RELAX_TAIL
                           ? "LDPC r0/JALR s0,r0"
                           : "LDPC r0/JALR s7,r0")
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
        if (!target.sym) {
          Err(ctx) << literalSec->getLocation(literalOffset)
                   << ": R_RISCC_CALL_TARGET has no target symbol";
          continue;
        }
        InputSection *targetSec = nullptr;
        uint64_t targetOffset = 0;
        if (auto *targetDef = dyn_cast_or_null<Defined>(target.sym);
            targetDef && targetDef->isSection()) {
          targetSec = dyn_cast_or_null<InputSection>(targetDef->section);
          int64_t offset = int64_t(targetDef->value) + target.addend;
          if (!targetSec || offset < 0 || uint64_t(offset) > targetSec->size) {
            Err(ctx) << literalSec->getLocation(literalOffset)
                     << ": R_RISCC_CALL_TARGET has an invalid section offset";
            continue;
          }
          targetOffset = offset;
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

  // A deletable call literal is owned by exactly one marker and its paired
  // LDPC relocation. Reject shared literals or any other live reference so
  // relaxation cannot remove data that is still in use.
  for (CallRelaxation &call : callRelaxations) {
    unsigned references = 0;
    for (InputSectionBase *base : ctx.inputSections) {
      auto *sec = dyn_cast<InputSection>(base);
      if (!sec || !sec->isLive())
        continue;
      for (auto [i, rel] : llvm::enumerate(sec->relocs())) {
        InputSection *targetSec;
        uint64_t targetOffset;
        if (!getInputSectionOffset(rel, targetSec, targetOffset) ||
            targetSec != call.literalSec ||
            targetOffset != call.literalSec->relocs()[call.literalRelocIndex]
                                .offset)
          continue;
        bool paired =
            sec == call.callSec &&
            (i == call.callRelocIndex || i == call.loadRelocIndex);
        if (!paired || ++references > 2)
          call.valid = false;
      }
    }
    if (!call.valid || references != 2)
      Err(ctx) << call.callSec->getLocation(
                      call.callSec->relocs()[call.callRelocIndex].offset)
               << ": relaxable RISC-C call literal is shared or has "
                  "unexpected references";
  }
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

uint32_t RISCC::droppedBefore(const InputSection &sec,
                              uint64_t offset) const {
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
        call.targetSec
            ? call.targetSec->getVA() + call.targetOffset -
                  droppedBefore(*call.targetSec, call.targetOffset)
            : call.target->getVA(ctx, call.targetAddend);
    bool candidate = !(target & 1) && target <= 0x1fffff;
    // Linker-script expressions can move a target in the opposite direction
    // when a section shrinks. After a few exploratory passes, allow only the
    // conservative relaxed-to-unrelaxed transition so address assignment
    // cannot oscillate indefinitely.
    if (pass < 4 || call.relaxed)
      call.relaxed = candidate;
  }

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
          write32le(p, rel.type == R_RISCC_RELAX_TAIL ? 0x00000034
                                                       : 0x00003834);
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

    Relocation &literal =
        relax.literalSec->relocs()[relax.literalRelocIndex];
    literal.expr = R_RELAX_HINT;
    literal.type = R_RISCC_NONE;
    literal.addend = 0;
  }
}

bool RISCC::checkCodeAddress(uint8_t *loc, const Relocation &rel,
                             uint64_t val) const {
  if (val > 0xffff) {
    Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
             << " cannot encode out-of-range byte address 0x"
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
  case R_RISCC_ABS8:
    checkUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_RISCC_ABS16:
    checkUInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_RISCC_ABS32:
  case R_RISCC_CALL_TARGET:
    checkUInt(ctx, loc, val, 32, rel);
    if (rel.type == R_RISCC_CALL_TARGET)
      checkAlignment(ctx, loc, val, 2, rel);
    write32le(loc, val);
    break;
  case R_RISCC_LO8:
    checkUInt(ctx, loc, val, 16, rel);
    *loc = val & 0xff;
    break;
  case R_RISCC_HI8:
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
    checkAlignment(ctx, loc, val, 2, rel);
    int64_t wordOffset = (static_cast<int64_t>(val) - 2) / 2;
    checkInt(ctx, loc, wordOffset, 8, rel);
    uint8_t encoded = static_cast<uint8_t>(wordOffset);
    *loc = static_cast<uint8_t>((encoded << 1) | (encoded >> 7));
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
  case R_RISCC_TPOFF32:
    checkUInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_RISCC_RELAX_CALL:
  case R_RISCC_RELAX_TAIL:
    break;
  default:
    llvm_unreachable("unknown RISC-C relocation");
  }
}

void elf::setRISCCTargetInfo(Ctx &ctx) { ctx.target.reset(new RISCC(ctx)); }
