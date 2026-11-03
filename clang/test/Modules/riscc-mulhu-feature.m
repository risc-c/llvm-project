// REQUIRES: riscc-registered-target
// RUN: rm -rf %t
// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -target-feature +mulhu \
// RUN:   -fmodules -fmodules-cache-path=%t/mulhu \
// RUN:   -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap \
// RUN:   -fsyntax-only %s
// RUN: not %clang_cc1 -triple riscc-none-elf -target-cpu full \
// RUN:   -fmodules -fmodules-cache-path=%t/base \
// RUN:   -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BASE
// RUN: not %clang_cc1 -triple riscc-none-elf -target-cpu full -target-feature +mulhu \
// RUN:   -DIMPORT_DIV -fmodules -fmodules-cache-path=%t/no-div \
// RUN:   -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=NO-DIV
// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -target-feature +mdu \
// RUN:   -DIMPORT_DIV -fmodules -fmodules-cache-path=%t/mdu \
// RUN:   -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap \
// RUN:   -fsyntax-only %s

@import RISCCFeatures.mulhu;
#ifdef IMPORT_DIV
@import RISCCFeatures.divu;
@import RISCCFeatures.mdu;
#endif

// BASE: module 'RISCCFeatures.mulhu' requires feature 'mulhu'
// NO-DIV: module 'RISCCFeatures.divu' requires feature 'divu'
// NO-DIV: module 'RISCCFeatures.mdu' requires feature 'mdu'
