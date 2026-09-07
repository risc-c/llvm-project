; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc -mattr=+rc32,-rc32 -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=RC16
; RUN: llc -mtriple=riscc -mattr=-rc32,+rc32 -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=RC32

; RC16: target datalayout = "e-m:e-p:16:16-
; RC32: target datalayout = "e-m:e-p:32:32-
define void @f() {
  ret void
}
