# Building Guide

This guide will help you build the project on your local machine. The process will require you to provide a decompressed ROM of the US version of the game.

These steps cover: decompressing the ROM, running the recompiler and finally building the project.

## 1. Clone the Zelda64Recomp Repository
This project makes use of submodules so you will need to clone the repository with the `--recurse-submodules` flag.

```bash
git clone --recurse-submodules
# if you forgot to clone with --recurse-submodules
# cd /path/to/cloned/repo && git submodule update --init --recursive
```

## 2. Install Dependencies

### Linux
For Linux the instructions for Ubuntu are provided, but you can find the equivalent packages for your preferred distro.

```bash
# For Ubuntu, simply run:
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev lld llvm clang
```

### Windows
You will need to install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/).
In the setup process you'll need to select the following options and tools for installation:
- Desktop development with C++
- C++ Clang Compiler for Windows
- C++ CMake tools for Windows

The other tool necessary will be `make` which can be installe via [Chocolatey](https://chocolatey.org/):
```bash
choco install make
```

## 3. Decompressing the target ROM
You will need to decompress the NTSC-U N64 Majora's Mask ROM (sha1: d6133ace5afaa0882cf214cf88daba39e266c078) before running the recompiler.

There are a few tools that can do it:
* This python script from the Majora's Mask decompilation project: https://github.com/zeldaret/mm/blob/main/tools/decompress_baserom.py
* https://github.com/z64tools/z64decompress

Regardless of which method you use, copy the decompressed ROM to the root of the Zelda64Recomp repository with this filename:
- `mm.us.rev1.rom_uncompressed.z64`

## 4. Generating the C code

Now that you have the required files, you must build [N64Recomp](https://github.com/Mr-Wiseguy/N64Recomp) and run it to generate the C code to be compiled. The building instructions can be found [here](https://github.com/Mr-Wiseguy/N64Recomp?tab=readme-ov-file#building). That will build the executables: `N64Recomp` and `RSPRecomp` which you should copy to the root of the Zelda64Recomp repository.

After that, go back to the repository root, and run the following commands:
```bash
./N64Recomp us.rev1.toml
./RSPRecomp aspMain.us.rev1.toml
./RSPRecomp njpgdspMain.us.rev1.toml
```

## 5. Building the Project

Finally, you can build the project! :rocket:

On Windows, you can open the repository folder with Visual Studio, and you'll be able to `[build / run / debug]` the project from there.

If you prefer the command line or you're on a Unix platform you can build the project using CMake:

```bash
cmake -S . -B build-cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -G Ninja -DCMAKE_BUILD_TYPE=Release # or Debug if you want to debug
cmake --build build-cmake --target Zelda64Recompiled -j$(nproc) --config Release # or Debug
```

## 6. Success

Voilà! You should now have a `Zelda64Recompiled` executable in the build directory! If you used Visual Studio this will be `out/build/x64-[Configuration]` and if you used the provided CMake commands then this will be `build-cmake`. You will need to run the executable out of the root folder of this project or copy the assets folder to the build folder to run it.

> [!IMPORTANT]
> In the game itself, you should be using a standard ROM, not the decompressed one.

## Building Ocarina of Time

Ocarina of Time (NTSC-U 1.0) is built as a separate executable, `Ocarina64Recompiled`, which shares the launcher, menus, input, graphics and audio code with Majora's Mask. Its recompilation config, symbols and patches live in the `oot` folder.

### 1. Decompressing the ROM
You will need to decompress the NTSC-U 1.0 N64 Ocarina of Time ROM (md5: 5bd1fe107bf8106b2ab6650abecd54d6). The [OoT decompilation project](https://github.com/zeldaret/oot)'s `tools/decompress_baserom.py` can do this (`make setup VERSION=ntsc-1.0 REGION=US` produces `baseroms/ntsc-1.0/baserom-decompressed.z64`). The decompressed ROM should have the md5 `6829a16db1a34e8ce989847cd8da8d9a`.

Copy the decompressed ROM to the root of the repository with this filename:
- `oot.us.rev0.rom_uncompressed.z64`

### 2. Generating the C code
Build `N64Recomp` and `RSPRecomp` as described above, then run the following from the `oot` folder:
```bash
../N64Recomp us.rev0.toml
../RSPRecomp aspMain.us.rev0.toml
../RSPRecomp njpgdspMain.us.rev0.toml
```

The symbol files in `oot/syms` are generated from the decompilation's ELF. See `oot/syms/generate.us.rev0.toml` for how to regenerate them.

### 3. Building
Once `oot/RecompiledFuncs` has been generated, CMake enables the `Ocarina64Recompiled` target (controlled by the `ZELDA64_BUILD_OOT` option). The `Zelda64Recompiled` (Majora's Mask) target is controlled by `ZELDA64_BUILD_MM`, which is off by default if only Ocarina of Time's code has been generated.

```bash
cmake --build build-cmake --target Ocarina64Recompiled -j$(nproc) --config Release
```

The patches in `oot/patches` are built with the same MIPS-capable `clang` and `ld.lld` as Majora's Mask's patches (`PATCHES_C_COMPILER` and `PATCHES_LD`).

`oot/patches/camera_mode_patches.c` is generated from the decompilation's camera code. After updating the `lib/oot-decomp` submodule, regenerate it from the repository root with `python oot/tools/gen_camera_patches.py lib/oot-decomp oot/patches/camera_mode_patches.c`.

> [!IMPORTANT]
> As with Majora's Mask, you should select the standard (compressed) ROM in the game itself, not the decompressed one.
