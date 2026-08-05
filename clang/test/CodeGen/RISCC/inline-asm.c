// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -emit-llvm -o - %s | \
// RUN:   FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -S -O0 -o %t.s %s
// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -S -O2 -o - %s | \
// RUN:   FileCheck %s --check-prefix=ASM

unsigned copy_register(unsigned value) {
  unsigned result;
  __asm__("mov %0, %1" : "=r"(result) : "r"(value));
  return result;
}

// IR-LABEL: define{{.*}} i16 @copy_register
// IR: call{{.*}} i16 asm "mov $0, $1", "=r,r"(i16
// ASM-LABEL: copy_register:
// ASM: mov r1, r1

unsigned add_immediate(unsigned value) {
  __asm__("addi %0, %1" : "+r"(value) : "i"(7));
  return value;
}

// IR-LABEL: define{{.*}} i16 @add_immediate
// IR: call{{.*}} i16 asm "addi $0, $1", "=r,i,0"(i16 7,
// ASM-LABEL: add_immediate:
// ASM: addi r1, 7

unsigned load_memory(const unsigned *address) {
  unsigned result;
  __asm__("ld %0, %1" : "=r"(result) : "m"(*address));
  return result;
}

// IR-LABEL: define{{.*}} i16 @load_memory
// IR: call{{.*}} i16 asm "ld $0, $1", "=r,*m"
// ASM-LABEL: load_memory:
// ASM: ld r1, [r1 + 0]

void store_memory(unsigned *address, unsigned value) {
  __asm__ volatile("st %1, %0" : "=m"(*address) : "r"(value));
}

// IR-LABEL: define{{.*}} void @store_memory
// IR: call{{.*}} void asm sideeffect "st $1, $0", "=*m,r"
// ASM-LABEL: store_memory:
// ASM: st r2, [r1 + 0]

unsigned add_registers(unsigned lhs, unsigned rhs) {
  unsigned result;
  __asm__("add %0, %1, %2" : "=&r"(result) : "r"(lhs), "r"(rhs));
  return result;
}

// IR-LABEL: define{{.*}} i16 @add_registers
// IR: call{{.*}} i16 asm "add $0, $1, $2", "=&r,r,r"
// ASM-LABEL: add_registers:
// ASM: add [[RESULT:r[0-6]]], r1, r2

unsigned add_in_place(unsigned lhs, unsigned rhs) {
  __asm__("add %0, %0, %1" : "+r"(lhs) : "r"(rhs));
  return lhs;
}

// IR-LABEL: define{{.*}} i16 @add_in_place
// IR: call{{.*}} i16 asm "add $0, $0, $1", "=r,r,0"
// ASM-LABEL: add_in_place:
// ASM: add r1, r1, r2

unsigned explicit_clobbers(unsigned value) {
  __asm__ volatile("mov r0, %0" : : "r"(value) : "r0", "memory");
  return value;
}

// IR-LABEL: define{{.*}} i16 @explicit_clobbers
// IR: call{{.*}} void asm sideeffect "mov r0, $0", "r,~{r0},~{memory}"
// ASM-LABEL: explicit_clobbers:
// ASM: mov r0, r1
