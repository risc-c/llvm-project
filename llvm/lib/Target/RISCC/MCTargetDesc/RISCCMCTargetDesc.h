#ifndef LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCMCTARGETDESC_H
#define LLVM_LIB_TARGET_RISCC_MCTARGETDESC_RISCCMCTARGETDESC_H

#include <cstdint>
#include <memory>

namespace llvm {
class Target;
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class MCStreamer;
class MCTargetStreamer;
class MCObjectWriter;
class Triple;

MCCodeEmitter *createRISCCMCCodeEmitter(const MCInstrInfo &, MCContext &);
MCAsmBackend *createRISCCMCAsmBackend(const Target &, const MCSubtargetInfo &,
                                      const MCRegisterInfo &,
                                      const MCTargetOptions &);
std::unique_ptr<MCObjectTargetWriter> createRISCCELFObjectWriter(uint8_t OSABI);
MCTargetStreamer *createRISCCObjectTargetStreamer(MCStreamer &,
                                                  const MCSubtargetInfo &);
MCStreamer *createRISCCELFStreamer(const Triple &, MCContext &,
                                   std::unique_ptr<MCAsmBackend> &&,
                                   std::unique_ptr<MCObjectWriter> &&,
                                   std::unique_ptr<MCCodeEmitter> &&);
}

#define GET_REGINFO_ENUM
#include "RISCCGenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "RISCCGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "RISCCGenSubtargetInfo.inc"

#endif
