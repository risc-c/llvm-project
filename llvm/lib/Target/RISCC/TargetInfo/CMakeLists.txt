add_llvm_component_library(LLVMRISCCInfo
  RISCCTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  RISCC
  )
