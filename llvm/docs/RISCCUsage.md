# User Guide for the RISC-C Target

RISC-C is an experimental, little-endian target for small freestanding systems.
The backend supports the RC16 and RC32 instruction sets and the separate RC16
Nano profile. It uses the `riscc-none-elf` triple and ELF32 objects for all three
configurations. The target implementation is in `llvm/lib/Target/RISCC`.

The project maintains the [ISA specification](https://github.com/risc-c/riscc/blob/main/doc/RISC-C-ISA.md),
[C and object ABI](https://github.com/risc-c/riscc/blob/main/doc/RISC-C-ABI.md), and
[programming guide](https://github.com/risc-c/riscc/blob/main/doc/PROGRAMMING.md).
The same repository contains an instruction-set simulator, Verilog cores,
startup code, and runtime libraries. The ABI is provisional; the compiler
supports the subset described below.

## Building and selecting the target

Enable the backend with `-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=RISCC`. Include
`clang` and `lld` in `LLVM_ENABLE_PROJECTS` to build the frontend and linker.
For example, compile a freestanding RC16 source file with:

```sh
clang --target=riscc-none-elf -mcpu=full -ffreestanding -O2 -c example.c
```

Add `-mrc32` for RC32. With `llc` and `llvm-mc`, select RC32 using `-mattr=+rc32`
instead. `-mcpu=nano` selects the incompatible RC16 Nano profile. RC32 and Nano
cannot be combined. RC32X is not implemented.

`llvm-objdump` selects the width and profile from the ELF header; `--mcpu` and
`--mattr` can override those defaults. The ELF flags do not record the optional
`mulhu` and `mdu` extensions, which require `--mattr=+mulhu` or `--mattr=+mdu`.

The `-mcpu` profiles are:

| Profile | Instructions added to the base ISA |
|---|---|
| `min` | None |
| `sys` | Interrupt/system instructions and long direct transfers |
| `full` (default) | `sys`, shifts by 2 through 8, and low-half multiply |
| `nano` | Separate RC16 encoding and calling convention |

Optional features include `mul`, `mulhu`, and `mdu`; the last enables paired
unsigned multiply and divide/remainder. Clang currently requires `-mcpu=full`
with `-mmdu`. Function `target-cpu` and
`target-features` attributes can change instruction availability, but cannot
change the module's register width or switch between the Nano and mainline ABIs.
When reassembling text containing several function profiles, select a profile
that supports all of its instructions. Object emission records the highest
required mainline profile in the ELF flags.

## Data model and runtime

RC16 has 16-bit `int` and pointers, 32-bit `long`, and two-byte stack alignment.
RC32 uses ILP32 and four-byte stack alignment. Both have 64-bit `long long`,
32-bit `float`, and 64-bit `double` and `long double`. Floating-point arithmetic
uses runtime calls. Wider integer arithmetic and instructions absent from a
selected profile also use runtime helpers.

The backend supports freestanding calls, variadic functions, and static ELF
links. Mainline profiles also support local-exec TLS. Applications provide
startup code, a linker script, and matching runtime libraries. The Clang driver
can use the project sysroot;
LLVM alone does not supply that runtime. Local-exec TLS accesses the current
context through `__riscc_current_context`.

Dynamic stack allocation, stack realignment, and raw LLVM IR `byval` arguments
are not implemented. Ordinary C aggregate arguments are lowered by Clang using
integer coercions according to the target ABI. Dynamic TLS,
PIC, code models other than small, dynamic linking, exceptions, unwinding, and
a C++ runtime are outside the supported ABI. The ELF machine number is currently
provisional.

## Backend structure

Instruction selection uses SelectionDAG and the usual LLVM register allocator.
The mainline S registers can transfer values to and from general registers but
cannot serve as ordinary arithmetic operands. After register allocation,
`RISCCSRegAllocator` assigns suitable spill slots and callee-saved values to
available S registers. Debug locations referring to removed spill slots are
marked unavailable.

RC32 loads large constants and addresses from nearby literal words.
`RISCCConstantIslandPass` places those words in unreachable machine blocks,
sharing entries within load range. It prefers gaps after unconditional
transfers, then block boundaries and shallower loops. The pass runs late and
calls generic branch relaxation, repairing literal references when code grows.
The assembly printer emits the resulting layout. Each relaxable direct call
owns its address word so lld can remove that word when shortening the call.

## Tests

Target tests are under `llvm/test/CodeGen/RISCC`, `llvm/test/MC/RISCC`, and
`clang/test/CodeGen/RISCC`, with driver, preprocessor, module, and semantic tests
in their corresponding Clang directories. Linker tests use the
`lld/test/ELF/riscc-*` prefix. The project repository additionally compares
compiled programs against its simulator and Verilator models.
