# mwccwrap

Command-line wrapper for CodeWarrior compiler plugin DLLs.

## Background

CodeWarrior for PlayStation shipped its C/C++ compiler only as an IDE plugin
DLL (`cc_mips.dll`) — no standalone `mwcc` command-line executable was ever
provided for the PS1 target. Later CW MIPS targets (PS2, PSP) did ship CLI
tools (`mwccps2.exe`, etc.), but PS1 never got one. A similar gap exists for
CW Wii 1.2, where the only available installer is truncated and missing
`mwcceppc.exe`, while `ppc_eabi.dll` is intact.

**mwccwrap** fills this gap. It builds a Win32 CLI host (`mwccwrap.exe`) that
loads a CW compiler DLL and drives it through the CW plugin lifecycle, plus
replacement DLLs that implement the API callbacks the compiler DLL imports.
This makes it possible to use these compilers from the command line and
from build systems, enabling decompilation projects and other workflows
that need reproducible builds with the original compiler.

For usage on Linux or macOS, use [wibo](https://github.com/decompals/wibo) (a
lightweight Win32 userspace emulator) or Wine.

## Supported compilers

| Platform | DLL | Versions |
|----------|-----|----------|
| PlayStation | `cc_mips.dll` | CW PS R3, R4, R4.1, R5, R5.2 |
| GameCube | `ppc_eabi.dll` | CW GC 1.1 through 2.7 |

## Building

Requires an `i686-w64-mingw32-gcc` cross-compiler (MinGW-w64).

```sh
make          # builds mwccwrap.exe and shim DLLs
make clean    # remove build artifacts
```

### Build outputs

| File | Description |
|------|-------------|
| `mwccwrap.exe` | CLI host executable |
| `PluginLib2.dll` | CW API shim for 1997-era DLLs |
| `PluginLib3.dll` | CW API shim for 1998-2005-era DLLs |
| `ASINTPPC.DLL` | Mac OS Toolbox API shim |

## Usage

```sh
# Basic compilation (requires cc_mips.dll or ppc_eabi.dll in cwd)
wibo ./mwccwrap.exe -o output.o input.c

# Specify compiler DLL explicitly
wibo ./mwccwrap.exe -dll /path/to/cc_mips.dll -o output.o input.c

# Common flags
wibo ./mwccwrap.exe -O2 -sym on -lang c -I include/ -D MY_DEFINE -o output.o input.c

# See all options
wibo ./mwccwrap.exe -help
```

### Runtime requirements

A compiler DLL must be available either:

- In the current working directory (`cc_mips.dll` or `ppc_eabi.dll`), or
- Passed explicitly via `-dll <path>`

## License

This project is licensed under the MIT License. See `LICENSE` for details.
