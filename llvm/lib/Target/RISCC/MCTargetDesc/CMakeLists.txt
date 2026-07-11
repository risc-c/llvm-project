add_llvm_component_library(LLVMRISCCDesc
  RISCCAsmBackend.cpp
  RISCCELFObjectWriter.cpp
  RISCCELFStreamer.cpp
  RISCCInstPrinter.cpp
  RISCCMCAsmInfo.cpp
  RISCCMCCodeEmitter.cpp
  RISCCMCExpr.cpp
  RISCCMCTargetDesc.cpp

  LINK_COMPONENTS
  BinaryFormat
  MC
  RISCCInfo
  Support

  ADD_TO_COMPONENT
  RISCC
  )
