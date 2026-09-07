; REQUIRES: riscc-registered-target
; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=riscc -mcpu=full %t/formal.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=riscc -mcpu=full -mattr=+rc32 %t/formal.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=riscc -mcpu=full %t/call.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=riscc -mcpu=full -mattr=+rc32 %t/call.ll -o /dev/null 2>&1 | FileCheck %s

; Clang passes aggregates as integer slots. Until the backend implements
; byval copies, raw IR must not pass a pointer to the caller's original object.
; CHECK: RISC-C does not support byval arguments

;--- formal.ll
%S = type { i16, i16 }
define void @formal(ptr byval(%S) align 2 %value) {
  store i16 7, ptr %value
  ret void
}

;--- call.ll
%S = type { i16, i16 }
declare void @callee(ptr byval(%S) align 2)
define void @call(ptr %value) {
  call void @callee(ptr byval(%S) align 2 %value)
  ret void
}
