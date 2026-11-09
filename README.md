# nnfck

Nintendo file check: a command-line tool that identifies Nintendo file formats
and prints what their headers say. It only reads files; nothing is extracted or
modified. Every format parser lives in this tree; the only external code is the
Zstandard decoder, included as a git submodule.

## Supported formats

| Format | Notes |
| --- | --- |
| Sead Yaz | Header, full stream decode and statistics. The compressed payload is identified and reported as "Sead Yaz-compressed (format)". |
| Zstandard | Frames, content and window sizes, dictionary and checksum flags. The payload is identified and reported as "Zstandard-compressed (format)". |
| Sharc Archive (SARC) | Header, SFAT/SFNT validation, name hash checks |
| NintendoWare Binary Cafe Resource (BFRES) | Wii U and Switch headers, every model, texture and animation with a one-line summary, embedded files |
| Binary Nintendo Texture (BNTX) | Header and every texture: size, format, mips, dimension, tiling, channel mapping |
| Sead Binary YAML (BYML) | Header, tables and root node |
| Havok Packfile | Header, layout rules, sections, contents class, concatenated packfiles |
| NintendoWare Effect Binary (PTCL: EFTB, VFXB) | header, root nodes, emitter set and emitter counts |

The Yaz magic is printed as stored (`"Yaz0"`) and not used as the format's name:
the trailing digit is left over from the N64, where it told cart data from disk
data.

## Building

nnfck builds with the LLVM toolchain (clang, lld, libc++) through CMake presets.
Fetch the Zstandard submodule first:

```sh
git submodule update --init
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Presets: `release`, `debug`, and `asan` (AddressSanitizer and UBSan).

## Usage

```sh
nnfck file.szs other.sbfres ...
```

Exit status: 0 when every file parsed cleanly, 1 when a file had parse errors,
2 for usage or I/O errors.
