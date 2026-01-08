# Build File Schema

The `catalyst.build` manifest defines the build graphs and arguments for build rules.
It uses a strict, pipe-delimited, 3-column format designed for efficient parsing.

## Structure

The file can have either of: Definitions or Build Steps.

### Definitions

Definitions configure the toolchain and environment variables.

Format: `DEF|<var>|<val>`

- `DEF`: The introducer for a definition line. Must be capitalized.
- `<var>`: The variable name (e.g., `cc`, `cflags`).
- `<val>`: The value assigned to the variable. Empty values use `DEF|<var>|`.

#### Supported Variables

| Variable | Description | Example |
| ---- | ---- | ---- |
| `cc` | The C compiler executable. | `/usr/bin/clang` |
| `cxx` | The C++ compiler executable. | `/usr/bin/clang++` |
| `cflags` | Flags passed to `cc` steps. | `-O2 -Wall` |
| `cxxflags` | Flags passed to `cxx` steps. | `-std=c++20 -O3` |
| `ldflags` | Linker search paths and general flags. | `-L/usr/local/lib` |
| `ldlibs` | Libraries to link against. | `-lpthread -lm` |

### Build Steps

Build steps define the actions to transform input files into output files.

Format: `<step_type>|<input_list>|<output_file>`

*   `<step_type>`: Mnemonic for the tool to use (see Toolchain Mapping).
*   `<input_list>`: Comma-separated list of input files.
*   `<output_file>`: The path to the generated file.

#### Toolchain Mapping

CBE maps specific step types to command templates. It strictly enforces certain behaviors (like dependency generation) by injecting flags.

| Type | Description | Command Template (Approximation) |
| ---- | ---- | ---- |
| `cc` | C Compile | `$cc $cflags -MMD -MF $out.d -c $in -o $out` |
| `cxx` | C++ Compile | `$cxx $cxxflags -MMD -MF $out.d -c $in -o $out` |
| `ld` | Binary Link | `$cxx $in -o $out $ldflags $ldlibs` |
| `ar` | Static Link | `ar rcs $out $in` |
| `sld` | Shared Link | `$cxx -shared $in -o $out` |

Important Notes(See [Implementation Details](../implementation/overview.md) for more info):

1.  Dependency Tracking (`.d`): For `cc` and `cxx`, __do not__ manually add `-MMD` or `-MF` to your `$cflags`/`$cxxflags`. CBE automatically injects these to manage incremental builds correctly.
2.  Response Files (`.rsp`): For `ld` steps with many inputs (currently >50), CBE will automatically generate a response file and pass it via `@<out>.rsp` to avoid command-line length limits.

## Example Manifest

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
