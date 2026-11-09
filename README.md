# nnfck

Nintendo file check: a command-line tool that identifies Nintendo file formats
and prints what their headers say. It only reads files; nothing is extracted or
modified. Every format parser lives in this tree; the only external code is the
Zstandard decoder, included as a git submodule.

## Supported formats

Every format's first field names the library that defines it: Sead,
NintendoWare, NintendoSDK, XLink2, EffectLink, SoundLink or Zstandard.

| Format | Notes |
| --- | --- |
| Yaz | Header, full stream decode and statistics. The compressed payload is identified and reported as "Yaz-compressed (format)". |
| Zstandard | Frames, content and window sizes, dictionary and checksum flags. The payload is identified and reported as "Zstandard-compressed (format)". |
| Sharc Archive (SARC) | Header, SFAT/SFNT validation, name hash checks |
| Binary Cafe Resource (BFRES) | Wii U and Switch headers, embedded files, and each resource block under its own format (Binary Cafe Model, Texture, Skeletal Animation and so on) with its offset and a one-line summary |
| Binary Nintendo Texture (BNTX) | Header and every texture: size, format, mips, dimension, tiling, channel mapping |
| Binary Cafe Layout (FLYT) | Wii U and Switch: layout name, origin and size, control, texture, font and material lists, the pane tree with parts layouts, groups |
| Binary Cafe Layout Animation (FLAN) | Wii U and Switch: tag name, frames and groups, shared animations, textures, and each animated pane or material with its animation kinds |
| Binary Cafe Complex Font (FCPX) | The font tree: pair fonts, bitmap fonts and scalable font faces with their character ranges |
| Cafe Layout Image (FLIM) | Wii U footer: size, texture format, GX2 tile mode and swizzle |
| Cafe Layout, Cafe Layout Animation, Cafe Complex Font | The XML sources of FLYT, FLAN and FCPX, listed like their binaries |
| Cafe Model, Cafe Texture, Cafe Skeletal Animation and other nw4f_3dif sources | Resource type, tool and info attributes, named arrays; the Binary variants (`*b`) also list their streams |
| Emitter Set (`.eset`) | Wii U and Switch EffectMaker sources: emitter tree, referenced textures and primitives |
| Binary YAML (BYML) | Header, tables and root node |
| Effect Binary (PTCL: EFTB, VFXB) | header, root nodes, emitter set and emitter counts |
| Binary XLink Database (XLNK) | ELink2 or SLink2 system, resource version, 32- or 64-bit positions, users (named in version 21), table counts, parameter definitions |
| Binary EffectLink Database (BLNK, eflk) | Pre-XLink effect links: users with their records, or their call tables, effects and actions |
| Binary SoundLink Database (SLNK) | Pre-XLink sound links: users with their call tables, assets, actions and action triggers |

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
