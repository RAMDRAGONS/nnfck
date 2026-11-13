# Nintendo file check

A command-line tool for parsing Nintendo file formats.

## Supported formats

Each report opens with the library that defines the format. Signatures are the
bytes a file begins with; formats that store theirs as a little-endian word
appear reversed. Where a format exists on several platforms, the report names
the one a file was built for. Switch shader files name Switch or Switch 2 from
the NVN control version their compiled code carries.

| Format                                                                                                   | Library                               | Signature              | Contents                                                                                                                                                                                                                        |
| -------------------------------------------------------------------------------------------------------- | ------------------------------------- | ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Yaz (`.szs`)                                                                                             | Sead                                  | `Yaz0`                 | Header, full stream decode and statistics, and the identified payload                                                                                                                                                           |
| Zstandard (`.zs`)                                                                                        | Zstandard                             | `28 B5 2F FD`          | Frames, content and window sizes, dictionary and checksum flags, and the identified payload                                                                                                                                     |
| Sharc Archive (`.sarc`, `.pack`)                                                                         | Sead                                  | `SARC`                 | Header, file table and name table checks, name hashes, and each file with its format                                                                                                                                            |
| Binary Cafe Resource (`.bfres`)                                                                          | NintendoWare                          | `FRES`                 | Wii U and Switch headers, embedded files, and each resource block (model, texture, skeletal animation and so on) with a one-line summary                                                                                        |
| Binary Nintendo Texture (`.bntx`)                                                                        | NintendoSDK                           | `BNTX`                 | Header and each texture's size, format, mips, dimension, tiling and channel mapping                                                                                                                                             |
| NVN Texture (`.xtx`, `.ntx`)                                                                             | NintendoSDK                           | `DFvN`                 | Version, blocks, and each texture's size, NVN format and target, mips and data size                                                                                                                                             |
| Binary Cafe Shader Archive (`.bfsha`)                                                                    | NintendoWare                          | `FSHA`                 | Wii U versions 3.4, 3.5 and 4.5 and Switch versions 3, 7 and 9: name, path, flags, and each shading model's programs, options, attributes, samplers, uniform blocks, uniforms and embedded shader file                          |
| Binary Nintendo Shader (`.bnsh`)                                                                         | NintendoSDK                           | `BNSH`                 | Target API, code type, compiler versions, variations by program kind, and code per stage                                                                                                                                        |
| NVN Shader                                                                                               | NintendoSDK                           | none                   | Bare NVN shader programs such as Splatoon 2's `nvn_font_shader.bin`: each stage's control and code sections                                                                                                                     |
| Shader Archive (`.sharc`)                                                                                | Agl                                   | `AAHS`                 | Versions 12 and 13: programs with the source each stage builds from and their macro and variation counts, and each source file's size                                                                                           |
| Binary Shader Archive (`.sharcfb`, `.sharcb`)                                                            | Agl                                   | `BAHS`                 | Wii U versions 8 and 9 and Switch version 9: shader binaries by stage, program names, and the Switch memory pool                                                                                                                |
| Binary Cafe Layout (`.bflyt`)                                                                            | NintendoWare                          | `FLYT`                 | Wii U and Switch: layout name, origin and size, texture, font and material lists, the pane tree with parts layouts, and groups                                                                                                  |
| Binary Cafe Layout Animation (`.bflan`)                                                                  | NintendoWare                          | `FLAN`                 | Wii U and Switch: tag, frames, groups, shared animations, textures, and each animated pane or material                                                                                                                          |
| Binary Cafe Complex Font (`.bfcpx`)                                                                      | NintendoWare                          | `FCPX`                 | The font tree: pair fonts, bitmap fonts and scalable font faces with their character ranges                                                                                                                                     |
| Cafe Layout Image (`.bflim`)                                                                             | NintendoWare                          | `FLIM` (footer)        | Wii U: size, texture format, GX2 tile mode and swizzle                                                                                                                                                                          |
| Cafe Layout, Cafe Layout Animation, Cafe Complex Font (`.flyt`, `.flan`, `.fcpx`)                        | NintendoWare                          | XML                    | The text sources of the binaries above, listed the same way                                                                                                                                                                     |
| Effect Binary (`.ptcl`)                                                                                  | NintendoWare; Sead for 3DS version 11 | `SPBD`, `EFTB`, `VFXB` | 3DS, Wii U and Switch: header, root nodes, and emitter set and emitter counts, with each 3DS emitter set's emitters                                                                                                             |
| Emitter Set (`.eset`)                                                                                    | NintendoWare                          | XML                    | Wii U and Switch: the emitter tree and referenced textures and primitives                                                                                                                                                       |
| Binary YAML (`.byml`)                                                                                    | Sead                                  | `BY`, `YB`             | Header, tables and root node                                                                                                                                                                                                    |
| Parameter Archive (`.baglenv`, `.bgenv` and others)                                                      | Agl                                   | `AAMP`, XML            | Wii U version 1, Switch version 2 and the XML text form: what the archive configures, data version, node and parameter counts and types, and the root's lists and objects by name, with text values checked against their types |
| Binary XLink Database (`.bslnk`, `.belnk`)                                                               | XLink2                                | `XLNK`                 | ELink2 or SLink2 system, resource version, 32- or 64-bit positions, users, table counts and parameter definitions                                                                                                               |
| Binary EffectLink Database                                                                               | EffectLink                            | `BLNK`, `eflk`         | Users with their records, or their call tables, effects and actions; 3DS files also share effect and action trigger pools and alias users                                                                                       |
| Binary SoundLink Database                                                                                | SoundLink                             | `SLNK`                 | Users with their call tables, assets, actions and action triggers; 3DS files also share asset and action trigger pools and alias users                                                                                          |
| Cafe Model, Cafe Texture, Cafe Skeletal Animation and other nw4f_3dif files (`.fmda`, `.fmdb` and so on) | NintendoWare                          | XML                    | Resource type, tool and info attributes and named arrays; binary variants also list their streams                                                                                                                               |

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

`cmake --install` places the binary under `bin` and data files under
`share/nnfck`. Builds copy the data files next to the executables so they run
in place.

## Data files

Parameter archives store names only as CRC-32 hashes. `data/pmaa_names.json`
lists the names that resolve them: `names` holds plain names, `indexed` and
`underscore_indexed` hold bases that take a numeric suffix (`Fog0`,
`blur_0`), from 0 up to `max_index`. Unknown hashes print in hex.

nnfck looks for data files in `$NNFCK_DATA_DIR`, then beside the executable,
then in `../share/nnfck` relative to it.

## Usage

```sh
nnfck file.szs other.sbfres ...
```

Exit status: 0 when every file parsed cleanly, 1 when a file had parse errors,
2 for usage or I/O errors.
