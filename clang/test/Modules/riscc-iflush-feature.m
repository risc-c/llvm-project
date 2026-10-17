// REQUIRES: riscc-registered-target
// RUN: rm -rf %t
// RUN: %clang_cc1 -triple riscc -target-cpu sys -target-feature +iflush -fmodules -fmodules-cache-path=%t/sys -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s
// RUN: %clang_cc1 -triple riscc -target-cpu full -target-feature +iflush -fmodules -fmodules-cache-path=%t/full -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s
// RUN: %clang_cc1 -triple riscc -target-cpu sys -target-feature +rc32 -target-feature +iflush -fmodules -fmodules-cache-path=%t/rc32-sys -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s
// RUN: %clang_cc1 -triple riscc -target-cpu full -target-feature +rc32 -target-feature +iflush -fmodules -fmodules-cache-path=%t/rc32-full -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s
// RUN: %clang_cc1 -triple riscc -target-cpu sys -target-feature -iflush -target-feature +iflush -fmodules -fmodules-cache-path=%t/enabled-last -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s
// RUN: %clang_cc1 -triple riscc -target-cpu min -target-feature +sys -target-feature +iflush -fmodules -fmodules-cache-path=%t/enabled-sys -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s
// RUN: not %clang_cc1 -triple riscc -target-cpu sys -fmodules -fmodules-cache-path=%t/default -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -triple riscc -target-cpu full -target-feature +iflush -target-feature -iflush -fmodules -fmodules-cache-path=%t/disabled-last -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -triple riscc -target-cpu sys -target-feature +iflush -target-feature -sys -fmodules -fmodules-cache-path=%t/disabled-sys -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -triple riscc -target-cpu min -target-feature +iflush -fmodules -fmodules-cache-path=%t/min -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -triple riscc -target-cpu nano -target-feature +sys -target-feature +iflush -fmodules -fmodules-cache-path=%t/nano -fmodule-map-file=%S/Inputs/RISCCFeatures/module.modulemap -fsyntax-only %s 2>&1 | FileCheck %s

@import RISCCFeatures.iflush;
// CHECK: module 'RISCCFeatures.iflush' requires feature 'iflush'
