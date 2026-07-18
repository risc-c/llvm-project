// RUN: %clang_cc1 -triple riscc-none-elf -target-cpu full -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s

// CHECK: target datalayout = "e{{.*}}P1{{.*}}p:16:16{{.*}}p1:16:16{{.*}}"
// CHECK: target triple = "riscc-unknown-none-elf"

typedef struct {
  unsigned short lo;
  unsigned short hi;
} pair_t;

typedef struct {
  unsigned short word[5];
} large_t;

// CHECK: @function_pointer = global ptr addrspace(1) @add_one, align 2
int add_one(int value) { return value + 1; }
int (*function_pointer)(int) = add_one;

// CHECK: define{{.*}} i16 @call_pointer(i16 noundef %value) addrspace(1)
// CHECK: call addrspace(1) i16 %{{.*}}(i16 noundef %{{.*}})
int call_pointer(int value) { return function_pointer(value); }

// A function designator is a program-space pointer; an explicit C cast to an
// object pointer becomes an addrspacecast for the backend to lower as << 1.
// CHECK: define{{.*}} ptr @function_as_data() addrspace(1)
// CHECK: ret ptr addrspacecast (ptr addrspace(1) @add_one to ptr)
void *function_as_data(void) { return (void *)add_one; }

// CHECK: define{{.*}} signext i8 @byte_argument(i8 noundef signext %value) addrspace(1)
signed char byte_argument(signed char value) { return value; }

// CHECK: define{{.*}} i32 @return_pair(i16 noundef %lo, i16 noundef %hi) addrspace(1)
pair_t return_pair(unsigned short lo, unsigned short hi) {
  pair_t value = {lo, hi};
  return value;
}

// Values larger than r1..r4 use the hidden result pointer in r1.
// CHECK: define{{.*}} void @return_large(ptr {{.*}}sret(%struct.large_t) align 2 %agg.result) addrspace(1)
large_t return_large(void) {
  large_t value = {{1, 2, 3, 4, 5}};
  return value;
}

typedef struct {
  unsigned char a, b;
} bytes2_t;

typedef struct __attribute__((packed)) {
  unsigned char value[3];
} bytes3_t;

typedef struct __attribute__((packed)) {
  unsigned char value[5];
} bytes5_t;

typedef struct {
  unsigned short value[4];
} bytes8_t;

typedef struct __attribute__((packed)) {
  unsigned char value[9];
} bytes9_t;

// Aggregate coercion pads to 16-bit ABI slots.  In particular, two byte
// fields occupy one word rather than being flattened into two registers.
// CHECK: define{{.*}} i16 @pass_bytes2(i16 %{{[^)]*}}) addrspace(1)
bytes2_t pass_bytes2(bytes2_t value) { return value; }

// CHECK: define{{.*}} i32 @pass_bytes3(i32 %{{[^)]*}}) addrspace(1)
bytes3_t pass_bytes3(bytes3_t value) { return value; }

// CHECK: define{{.*}} i48 @pass_bytes5(i48 %{{[^)]*}}) addrspace(1)
bytes5_t pass_bytes5(bytes5_t value) { return value; }

// CHECK: define{{.*}} i64 @pass_bytes8(i64 %{{[^)]*}}) addrspace(1)
bytes8_t pass_bytes8(bytes8_t value) { return value; }

// Returns larger than four words use sret; a by-value argument remains one
// direct padded value so SelectionDAG can place it wholly on the stack.
// CHECK: define{{.*}} void @pass_bytes9(ptr {{.*}}sret(%struct.bytes9_t){{.*}}, i80 %{{[^)]*}}) addrspace(1)
bytes9_t pass_bytes9(bytes9_t value) { return value; }

// CHECK: define{{.*}} zeroext i1 @pass_bool(i1 {{.*}}zeroext %{{[^)]*}}) addrspace(1)
_Bool pass_bool(_Bool value) { return value; }

// Soft-float scalars keep their LLVM floating type at the Clang ABI boundary;
// the backend later splits them into ordinary 16-bit argument/result slots.
// long double intentionally has the same representation as double.
// CHECK: define{{.*}} float @pass_float(float noundef %value) addrspace(1)
float pass_float(float value) { return value; }

// CHECK: define{{.*}} double @pass_double(double noundef %value) addrspace(1)
double pass_double(double value) { return value; }

// CHECK: define{{.*}} double @pass_long_double(double noundef %value) addrspace(1)
long double pass_long_double(long double value) { return value; }

typedef struct {
  float first;
  float second;
} float_pair_t;

// Floating members do not create a special aggregate convention.
// CHECK: define{{.*}} i64 @pass_float_pair(i64 %{{[^)]*}}) addrspace(1)
float_pair_t pass_float_pair(float_pair_t value) { return value; }
