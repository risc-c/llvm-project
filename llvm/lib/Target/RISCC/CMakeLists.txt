add_llvm_component_group(RISCC)

set(LLVM_TARGET_DEFINITIONS RISCC.td)

tablegen(LLVM RISCCGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM RISCCGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM RISCCGenCallingConv.inc -gen-callingconv)
tablegen(LLVM RISCCGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM RISCCGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM RISCCGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM RISCCGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM RISCCGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM RISCCGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(RISCCCommonTableGen)

add_llvm_target(RISCCCodeGen
  RISCCAsmPrinter.cpp
  RISCCCodeGenPassBuilder.cpp
  RISCCFrameLowering.cpp
  RISCCISelDAGToDAG.cpp
  RISCCISelLowering.cpp
  RISCCInstrInfo.cpp
  RISCCMCInstLower.cpp
  RISCCMachineFunctionInfo.cpp
  RISCCRegisterInfo.cpp
  RISCCSubtarget.cpp
  RISCCTargetMachine.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  IRPrinter
  MC
  RISCCDesc
  RISCCInfo
  ObjCARC
  Scalar
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  RISCC
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
