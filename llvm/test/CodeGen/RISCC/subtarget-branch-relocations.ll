; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mcpu=min -mattr=+rc32 -filetype=obj %s -o %t
; RUN: llvm-readobj -h -r %t | FileCheck %s

; A later Full function raises the object's profile. Keep earlier Min branch
; references relocatable because the linker may relax calls in this object.
; CHECK: EF_RISCC_PROFILE_FULL
; CHECK: EF_RISCC_RC32
; CHECK: R_RISCC_PCREL8_WORD

define void @min_branch() {
  call void asm sideeffect "jmp8 1f\0A1:", ""()
  ret void
}

define void @full_function() "target-cpu"="full" {
  ret void
}
