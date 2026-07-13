// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -std=c11 \
// RUN:   -fsyntax-only -verify %s

void vla(unsigned size) {
  unsigned values[size]; // expected-error {{variable length arrays are not supported}}
}
