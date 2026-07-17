// REQUIRES: riscc-registered-target
// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -std=c11 -fsyntax-only %s
// RUN: not %clang_cc1 -triple riscc-none-elf -target-cpu nano -std=c11 \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s

_Thread_local int value;

// CHECK: error: thread-local storage is not supported for the current target
