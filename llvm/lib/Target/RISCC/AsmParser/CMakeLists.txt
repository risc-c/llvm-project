add_llvm_component_library(LLVMRISCCAsmParser
  RISCCAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  RISCCDesc
  RISCCInfo
  Support

  ADD_TO_COMPONENT
  RISCC
  )
