// RUN: %clang_cc1 -mrelocation-model static -triple riscc -target-cpu full -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --implicit-check-not=byval
// RUN: %clang_cc1 -mrelocation-model static -triple riscc -target-cpu full -target-feature +rc32 -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --implicit-check-not=byval

struct Value {
  int Data;
  Value(const Value &);
  ~Value();
};

void consume(Value);

// Pass the constructed object by address, preserving its identity instead
// of coercing it to an integer or copying it through a byval parameter.
// CHECK-LABEL: define{{.*}} void @_Z4callR5Value(
// CHECK:       call void @_Z7consume5Value(ptr noundef align [[ALIGN:[24]]] %{{[^)]+}})
// CHECK:       declare dso_local void @_Z7consume5Value(ptr noundef align [[ALIGN]])
void call(Value &V) { consume(V); }
