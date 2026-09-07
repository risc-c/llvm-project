// RUN: %clang_cc1 -mrelocation-model static -triple riscc-none-elf -target-cpu full -target-feature +rc32 \
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

// ASM-LABEL: tls_load:
// ASM:       ldpc r0, [[CONTEXT:.Ltmp[0-9]+]]
// ASM-NEXT:  ld r0, [r0 + 0]
// ASM-NEXT:  ldpc r1, [[TLS:.Ltmp[0-9]+]]
// ASM-NEXT:  ldx r1, [r0 + r1]
// ASM:       ret s7
// ASM:       [[CONTEXT]]:
// ASM-NEXT:  .long __riscc_current_context
// ASM:       [[TLS]]:
// ASM-NEXT:  .long tpoff(tls)
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

// ASM-LABEL: shift_left:
// ASM:       ldpc r0, [[SHIFT:.Ltmp[0-9]+]]
// ASM:       [[SHIFT]]:
// ASM-NEXT:  .long call_target(__ashlsi3)
int shift_left(int value, int count) { return value << count; }

// The caller must extend 16-bit arguments to a complete RC32 register.
// IR: define{{.*}} signext i16 @pass_short(i16 noundef signext %value)
short pass_short(short value) { return value; }

// IR: define{{.*}} zeroext i16 @pass_ushort(i16 noundef zeroext %value)
unsigned short pass_ushort(unsigned short value) { return value; }

// A _BitInt can be narrower than a register despite occupying four bytes.
// IR: define{{.*}} zeroext i17 @pass_u17(i17 noundef zeroext %value)
unsigned _BitInt(17) pass_u17(unsigned _BitInt(17) value) { return value; }
