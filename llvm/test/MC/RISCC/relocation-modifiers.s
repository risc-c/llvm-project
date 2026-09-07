# REQUIRES: riscc-registered-target
# RUN: split-file %s %t
# RUN: not llvm-mc -triple=riscc -mcpu=full -filetype=obj %t/widths.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=WIDTH
# RUN: not llvm-mc -triple=riscc -mcpu=full -defsym=symbol=4660 -filetype=obj %t/widths.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=WIDTH
# RUN: not llvm-mc -triple=riscc -mcpu=full -filetype=obj %t/nested.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NESTED
# RUN: llvm-mc -triple=riscc -mcpu=full -filetype=obj %t/aliases.s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=ALIAS
# RUN: llvm-mc -triple=riscc -mcpu=full -defsym=symbol=4660 -filetype=obj %t/aliases.s -o %t.resolved.o
# RUN: llvm-readobj --hex-dump=.text %t.resolved.o | FileCheck %s --check-prefix=BYTES
# RUN: not llvm-mc -triple=riscc -mcpu=full -filetype=obj %t/ldi16-aliases.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LDI16
# RUN: not llvm-mc -triple=riscc -mcpu=full -defsym=symbol=4660 -filetype=obj %t/ldi16-aliases.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LDI16
# RUN: not llvm-mc -triple=riscc -filetype=obj %t/quad.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=QUAD
# RUN: llvm-mc -triple=riscc -defsym=symbol=4660 -filetype=obj %t/quad.s -o %t.quad.o
# RUN: llvm-readobj --hex-dump=.text %t.quad.o | FileCheck %s --check-prefix=QUAD-BYTES

#--- widths.s
.short lo8(symbol)
.short hi8(symbol)
.long code(symbol)
.short code_lo8(symbol)
.short code_hi8(symbol)
.byte tpoff(symbol)
.short call_target(symbol)
# WIDTH-COUNT-7: error: relocation modifier is not valid for this field
# WIDTH-NOT: error:

#--- nested.s
.byte lo8(hi8(symbol))
ldi r0, lo8(lo8(symbol))
.set low, lo8(symbol)
.byte hi8(low)
ldi r0, lo8(forward)
.set forward, lo8(symbol)
# NESTED-COUNT-4: error: expected relocatable expression
# NESTED-NOT: error:

#--- aliases.s
# Aliases retain the modifier, including when it differs from LDI's default.
.set low, lo8(symbol)
.set high, hi8(symbol)
ldi r0, low
ldi r0, high
lui r0, high
.byte low, high
# ALIAS: 0x0 R_RISCC_LO8 symbol
# ALIAS: 0x2 R_RISCC_HI8 symbol
# ALIAS: 0x4 R_RISCC_HI8 symbol
# ALIAS: 0x6 R_RISCC_LO8 symbol
# ALIAS: 0x7 R_RISCC_HI8 symbol
# BYTES: 34801280 12813412

#--- ldi16-aliases.s
.set low, lo8(symbol)
.set high, hi8(symbol)
ldi16 r0, low
ldi16 r0, high
# LDI16-COUNT-2: error: LDI16 accepts only an unmodified, code(), or tpoff() expression
# LDI16-NOT: error:

#--- quad.s
.quad symbol
# QUAD: error: RISC-C does not support 64-bit relocations
# QUAD-NOT: error:
# QUAD-BYTES: 34120000 00000000
