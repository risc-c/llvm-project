// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -fsyntax-only -verify %s
// expected-no-diagnostics

#include <stdarg.h>

void sink(unsigned int, ...);

void test(unsigned int fixed, ...) {
  va_list ap;
  va_start(ap, fixed);
  sink(1, 2);
  (void)va_arg(ap, unsigned int);
  va_end(ap);
}
