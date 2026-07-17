// REQUIRES: riscc-registered-target

// RUN: rm -rf %t
// RUN: not %clang_cc1 -triple riscc-none-elf -target-cpu min -fmodules \
// RUN:   -fmodules-cache-path=%t/min -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=MIN
// RUN: not %clang_cc1 -triple riscc-none-elf -target-cpu sys -fmodules \
// RUN:   -fmodules-cache-path=%t/sys -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=SYS
// RUN: not %clang_cc1 -triple riscc-none-elf -target-cpu full -fmodules \
// RUN:   -fmodules-cache-path=%t/full -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=FULL

@import RISCCFeatures.riscc;
@import RISCCFeatures.min;
@import RISCCFeatures.sys;
@import RISCCFeatures.system;
@import RISCCFeatures.jal16;
@import RISCCFeatures.mul;
@import RISCCFeatures.full;

// MIN: module 'RISCCFeatures.sys' requires feature 'sys'
// MIN: module 'RISCCFeatures.system' requires feature 'system'
// MIN: module 'RISCCFeatures.jal16' requires feature 'jal16'
// MIN: module 'RISCCFeatures.mul' requires feature 'mul'
// MIN: module 'RISCCFeatures.full' requires feature 'full'

// SYS: module 'RISCCFeatures.min' requires feature 'min'
// SYS: module 'RISCCFeatures.mul' requires feature 'mul'
// SYS: module 'RISCCFeatures.full' requires feature 'full'

// FULL: module 'RISCCFeatures.min' requires feature 'min'
