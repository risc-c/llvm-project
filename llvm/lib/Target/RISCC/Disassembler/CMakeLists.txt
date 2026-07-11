add_llvm_component_library(LLVMRISCCDisassembler
  RISCCDisassembler.cpp

  LINK_COMPONENTS
  MC
  MCDisassembler
  RISCCDesc
  RISCCInfo
  Support

  ADD_TO_COMPONENT
  RISCC
  )
