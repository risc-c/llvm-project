// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -target-feature +rc32 \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=riscc-none-elf -mrc32 -S -O2 %s -o - | \
// RUN:   FileCheck %s --check-prefix=ASM

// IR: target datalayout = "e{{.*}}p:32:32{{.*}}"

typedef struct {
  int word[3];
} three_words_t;

typedef struct {
  int word[4];
} four_words_t;

// IR: define{{.*}} i96 @pass_three_words(i96 %{{[^)]*}})
three_words_t pass_three_words(three_words_t value) { return value; }

// IR: define{{.*}} void @pass_four_words(ptr {{.*}}sret(%struct.four_words_t) align 4 %{{[^)]*}}, i128 %{{[^)]*}})
four_words_t pass_four_words(four_words_t value) { return value; }

__thread int tls;

// ASM:       [[TLS:.Ltmp[0-9]+]]:
// ASM-NEXT:  .long tpoff(tls)
// ASM-LABEL: tls_load:
// ASM:       mfs r0, s2
// ASM:       ldpc r1, [[TLS]]
// ASM-NEXT:  ldx r1, [r0 + r1]
// ASM:       rets
int tls_load(void) { return tls; }

#include <stdarg.h>

// ASM-LABEL: first_vararg:
// ASM:       addi r7, -8
// ASM:       ld r1, [r7 + 8]
// ASM:       addi r7, 8
int first_vararg(int count, ...) {
  va_list args;
  va_start(args, count);
  return va_arg(args, int);
}

// ASM:       [[SHIFT:.Ltmp[0-9]+]]:
// ASM-NEXT:  .long __ashlsi3
// ASM-LABEL: shift_left:
// ASM:       ldpc r0, [[SHIFT]]
int shift_left(int value, int count) { return value << count; }
