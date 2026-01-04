# CBE (Catalyst Build Executor)

[CBE](https://github.com/S-Spektrum-M/catalyst-build-executor) is a high-speed, low-level build orchestrator
designed as a machine-generated target for higher-level meta-build systems, focusing on performance and
dependency tracking.

## The `catalyst.build` Schema

The manifest uses a strict pipe-delimited, 3-column format.

### Definitions

Definitions configure the toolchain and must appear at the top of the file. Empty definitions use `DEF|<var>|`.

| Variable | Description |
| --- | --- |
| `DEF\|cc\|<val>` | The C compiler (e.g., `/usr/bin/clang`). |
| `DEF\|cxx\|<val>` | The C++ compiler (e.g., `/usr/bin/clang++`). |
| `DEF\|cflags\|<val>` | Flags for `cc` steps. |
| `DEF\|cxxflags\|<val>` | Flags for `cxx` steps. |
| `DEF\|ldflags\|<val>` | Linker search paths and general flags. |
| `DEF\|ldlibs\|<val>` | Libraries to link against (e.g., `-lpthread`). |

### Build Steps

Format: `<step_type>|<input_list>|<output_file>`

*   `step_type`: Tool mnemonic (see Toolchain Mapping).
*   `input_list`: Comma-separated files (e.g., `src/main.cpp,src/util.cpp`).
*   `output_file`: The generated file.

Note: Future support for `.rsp` (response files) is planned for handling long input lists.

### Toolchain Mapping & Dependencies

CBE manages dependency tracking (`.d` files) automatically.

| Key | Type | Command Template |
| --- | --- | --- |
| `cc` | C Compile | `$cc $cflags -MMD -MF $out.d -c $in -o $out` |
| `cxx` | C++ Compile | `$cxx $cxxflags -MMD -MF $out.d -c $in -o $out` |
| `ld` | Binary Link | `$cxx $in -o $out $ldflags $ldlibs` |
| `ar` | Static Link | `ar rcs $out $in` |
| `sld` | Shared Link | `$cxx -shared $in -o $out` |

`cc` and `cxx` steps are expected to produce a sidecar `.d` file at `<output_file>.d` for dependency graph population.

## Example `catalyst.build`

```text
DEF|cc|clang
DEF|cxx|clang++
DEF|cflags|
DEF|cxxflags|-std=c++20 -O3
DEF|ldflags|
DEF|ldlibs|
cxx|src/main.cpp|build/main.o
cxx|src/net.cpp|build/net.o
ld|build/main.o,build/net.o|build/app
```
