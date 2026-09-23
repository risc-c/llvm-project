; REQUIRES: riscc-registered-target
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC16-MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC16-FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC32-MIN
; RUN: llc -mtriple=riscc-none-elf -mcpu=full -mattr=+rc32 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC32-FULL
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC16-MASK
; RUN: llc -mtriple=riscc-none-elf -mcpu=min -mattr=+rc32 -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=RC32-MASK

; A one-bit result can use the range comparison, while a multi-bit result
; keeps its shift.  The full profile has a native wide shift.
define i16 @extract_narrow16(i16 %x) {
; RC16-MIN-LABEL: extract_narrow16:
; RC16-MIN:       andi {{r[0-7]}}, 16
; RC16-MIN-NOT:   srli
; RC16-MIN:       ldi {{r[0-7]}}, 15
; RC16-MIN:       sltu
; RC16-FULL-LABEL: extract_narrow16:
; RC16-FULL:       andi {{r[0-7]}}, 16
; RC16-FULL-NOT:   srli
; RC16-FULL:       ldi {{r[0-7]}}, 15
; RC16-FULL:       sltu
  %masked = and i16 %x, 31
  %result = lshr i16 %masked, 4
  ret i16 %result
}

define i16 @extract_multibit16(i16 %x) {
; RC16-MIN-LABEL: extract_multibit16:
; RC16-MIN:       srli
; RC16-MIN-NOT:   sltu
; RC16-MIN:       ret
; RC16-FULL-LABEL: extract_multibit16:
; RC16-FULL:       srli {{r[0-7]}}, {{r[0-7]}}, 4
; RC16-FULL-NOT:   sltu
; RC16-FULL:       ret
  %masked = and i16 %x, 63
  %result = lshr i16 %masked, 4
  ret i16 %result
}

define i32 @extract_narrow32(i32 %x) {
; RC32-MIN-LABEL: extract_narrow32:
; RC32-MIN:       andi {{r[0-7]}}, 16
; RC32-MIN-NOT:   srli
; RC32-MIN:       ldi {{r[0-7]}}, 15
; RC32-MIN:       sltu
; RC32-FULL-LABEL: extract_narrow32:
; RC32-FULL:       andi {{r[0-7]}}, 16
; RC32-FULL-NOT:   srli
; RC32-FULL:       ldi {{r[0-7]}}, 15
; RC32-FULL:       sltu
  %masked = and i32 %x, 31
  %result = lshr i32 %masked, 4
  ret i32 %result
}

define i32 @extract_multibit32(i32 %x) {
; RC32-MIN-LABEL: extract_multibit32:
; RC32-MIN:       srli
; RC32-MIN-NOT:   sltu
; RC32-MIN:       ret
; RC32-FULL-LABEL: extract_multibit32:
; RC32-FULL:       srli {{r[0-7]}}, {{r[0-7]}}, 4
; RC32-FULL-NOT:   sltu
  %masked = and i32 %x, 63
  %result = lshr i32 %masked, 4
  ret i32 %result
}

; In minsize mode the comparison is kept only when its byte cost is no
; greater than the shift sequence.  Native wide shifts win on full.
define i16 @extract_minsize16(i16 %x) #0 {
; RC16-MIN-LABEL: extract_minsize16:
; RC16-MIN:       sltu
; RC16-FULL-LABEL: extract_minsize16:
; RC16-FULL:      srli {{r[0-7]}}, {{r[0-7]}}, 4
; RC16-FULL-NOT:  sltu
  %masked = and i16 %x, 31
  %result = lshr i16 %masked, 4
  ret i16 %result
}

define i32 @extract_minsize32(i32 %x) #0 {
; RC32-MIN-LABEL: extract_minsize32:
; RC32-MIN:       ldi {{r[0-7]}}, 15
; RC32-MIN:       sltu
; RC32-FULL-LABEL: extract_minsize32:
; RC32-FULL:      srli {{r[0-7]}}, {{r[0-7]}}, 4
; RC32-FULL-NOT:  sltu
; RC32-FULL:      ret
  %masked = and i32 %x, 31
  %result = lshr i32 %masked, 4
  ret i32 %result
}

; An OR user is a funnel-shift candidate; the range compare must not consume
; one half of the funnel.
define i16 @funnel16(i16 %x, i16 %other) {
; RC16-MIN-LABEL: funnel16:
; RC16-MIN:       srli
; RC16-MIN:       or
; RC16-MIN-NOT:   sltu
; RC16-MIN:       ret
; RC16-FULL-LABEL: funnel16:
; RC16-FULL:      srli
; RC16-FULL:      or
; RC16-FULL-NOT:  sltu
; RC16-FULL:      ret
  %masked = and i16 %x, 31
  %part = lshr i16 %masked, 4
  %result = or i16 %part, %other
  ret i16 %result
}

define i32 @funnel32(i32 %x, i32 %other) {
; RC32-MIN-LABEL: funnel32:
; RC32-MIN:       srli
; RC32-MIN:       or
; RC32-MIN-NOT:   sltu
; RC32-MIN:       ret
; RC32-FULL-LABEL: funnel32:
; RC32-FULL:      srli
; RC32-FULL:      or
; RC32-FULL-NOT:  sltu
; RC32-FULL:      ret
  %masked = and i32 %x, 31
  %part = lshr i32 %masked, 4
  %result = or i32 %part, %other
  ret i32 %result
}

; The comparison simplifier also sees a value assembled through a PHI after
; a byte load.  Keep this at O0 so the load and PHI remain visible.
define i16 @masked_byte_phi16(ptr %p, i16 %x, i1 %condition) {
; RC16-MASK-LABEL: masked_byte_phi16:
; RC16-MASK:       ldb
; RC16-MASK:       ldi {{r[0-7]}}, 255
; RC16-MASK:       sltu
; RC16-MASK:       xori {{r[0-7]}}, 1
  br i1 %condition, label %byte, label %other

byte:
  %raw = load i8, ptr %p
  %value = zext i8 %raw to i16
  br label %join

other:
  br label %join

join:
  %phi = phi i16 [ %value, %byte ], [ %x, %other ]
  %masked = and i16 %phi, 255
  %equal = icmp eq i16 %masked, %phi
  %result = zext i1 %equal to i16
  ret i16 %result
}

define i32 @masked_byte_phi32(ptr %p, i32 %x, i1 %condition) {
; RC32-MASK-LABEL: masked_byte_phi32:
; RC32-MASK:       ldb
; RC32-MASK:       ldi {{r[0-7]}}, 255
; RC32-MASK:       sltu
; RC32-MASK:       xori {{r[0-7]}}, 1
  br i1 %condition, label %byte, label %other

byte:
  %raw = load i8, ptr %p
  %value = zext i8 %raw to i32
  br label %join

other:
  br label %join

join:
  %phi = phi i32 [ %value, %byte ], [ %x, %other ]
  %masked = and i32 %phi, 255
  %equal = icmp eq i32 %masked, %phi
  %result = zext i1 %equal to i32
  ret i32 %result
}

attributes #0 = { minsize }

; A variable mask or holes in a constant mask cannot define one unsigned range.
define i16 @variable_mask16(i16 %x, i16 %mask) {
; RC16-MASK-LABEL: variable_mask16:
; RC16-MASK:       and
  %masked = and i16 %x, %mask
  %equal = icmp eq i16 %masked, %x
  %result = zext i1 %equal to i16
  ret i16 %result
}

define i32 @variable_mask32(i32 %x, i32 %mask) {
; RC32-MASK-LABEL: variable_mask32:
; RC32-MASK:       and
  %masked = and i32 %x, %mask
  %equal = icmp eq i32 %masked, %x
  %result = zext i1 %equal to i32
  ret i32 %result
}

define i16 @noncontiguous_mask16(i16 %x) {
; RC16-MASK-LABEL: noncontiguous_mask16:
; RC16-MASK:       andi {{r[0-7]}}, 85
  %masked = and i16 %x, 85
  %different = icmp ne i16 %masked, %x
  %result = zext i1 %different to i16
  ret i16 %result
}

define i32 @noncontiguous_mask32(i32 %x) {
; RC32-MASK-LABEL: noncontiguous_mask32:
; RC32-MASK:       andi {{r[0-7]}}, 85
  %masked = and i32 %x, 85
  %different = icmp ne i32 %masked, %x
  %result = zext i1 %different to i32
  ret i32 %result
}
