#!/usr/bin/env python3
"""Generates the inputs for recompiling Ocarina of Time with gz (the practice ROM) built in.

gz is a payload that runs from 0x80400000 and hooks into the game by replacing a few instructions with calls into
it. This script does the equivalent of gz's `make-rom` for the recompiler:

  - Applies gz's hooks and memory patch to the uncompressed ROM, and appends the gz image to it, so that the
    recompiled game calls into gz directly.
  - Adds a section with gz's functions (taken from gz.elf) to the symbols file.
  - Writes a recompiler config that stubs out gz's hardware-specific code (flashcarts, SD cards, the remote debugger)
    and gates gz behind the "Start gz" launcher option.
  - Writes the gz image and its layout so the runtime can load it into memory at boot.

gz must be built for oot-1.0 without LTO, as LTO reintroduces division by zero traps. See BUILDING.md.

Usage: gen_gz.py [--recompile <N64Recomp path>] <gz repo dir> [gz build dir]

With --recompile, N64Recomp is run on the generated config when any of the outputs changed.
"""

import os
import re
import struct
import subprocess
import sys
import tomllib

OOT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT_DIR = os.path.join(OOT_DIR, "gz")

BASE_CONFIG = os.path.join(OOT_DIR, "us.rev0.toml")
BASE_SYMS = os.path.join(OOT_DIR, "syms", "oot.us.rev0.syms.toml")
BASE_ROM = os.path.join(OOT_DIR, "..", "oot.us.rev0.rom_uncompressed.z64")

# Prefix for gz's function names, as gz links its own libc (memcpy, malloc, etc.) and has a `main`.
FUNC_PREFIX = "gz__"

# Where the gz image is placed in the ROM given to the recompiler. It isn't in the real ROM, and the runtime copies
# the image into memory itself, so this only needs to be past the end of the game's data.
GZ_ROM_ALIGN = 0x100000

# Native function that returns whether gz was started from the launcher, called from recompiled code.
GZ_ACTIVE_FUNC = "zelda64_gz_active"

# gz's hooks, as in gz's `genhooks` script. Each entry is the hook site's symbol in gz's liboot linker script and
# the instructions to write there: either the name of a gz function to call, "nop", or a raw instruction word.
HOOKS = [
    ("z64_main_hook", ["_start"]),
    ("z64_entrance_offset_hook", ["entrance_offset_hook", "nop"]),
    ("z64_draw_room_1_call", ["draw_room_hook"]),
    ("z64_draw_room_2_call", ["draw_room_hook"]),
    ("z64_draw_actors_call", ["draw_actors_hook"]),
    ("z64_srand_call", ["srand_hook"]),
    ("z64_frame_input_call", ["input_hook", "nop"]),
    ("z64_ocarina_update_call", ["ocarina_update_hook"]),
    ("z64_ocarina_input_call", ["ocarina_input_hook"]),
    ("z64_ocarina_sync", ["nop"]),
    ("z64_afx_rand_call", ["afx_rand_hook"]),
    ("z64_room_load_sync_hook", ["room_load_sync_hook"]),
    ("z64_camera_hook", ["camera_hook"]),
    ("z64_guPerspectiveF_hook", ["guPerspectiveF_hook", 0xAFA60038]),  # sw $a2, 0x38($sp)
    ("z64_guPerspective_camo", ["nop", "nop"]),
    ("z64_disp_swap_1", ["disp_hook"]),
    ("z64_disp_swap_2", ["disp_hook"]),
    ("z64_disp_swap_3", ["disp_hook"]),
    ("z64_disp_swap_4", ["disp_hook"]),
    ("z64_metronome_start_1", ["metronome_start_hook"]),
    ("z64_metronome_start_2", ["metronome_start_hook"]),
    ("z64_metronome_check_1", ["metronome_check_hook"]),
    ("z64_metronome_check_2", ["metronome_check_hook"]),
    ("z64_metronome_check_3", ["metronome_check_hook"]),
    ("z64_metronome_check_4", ["metronome_check_hook"]),
    ("z64_bombchu_floor_poly_hook", ["bombchu_floor_poly_hook"]),
]

# gz's mem_patch.gsc: limits the game to the first 4MB of RAM so it doesn't overlap gz.
MEM_PATCH = [(0x800004D4, 0x3C020040)]  # lui $v0, 0x0040

# gz functions implemented natively (see src/game/gz.cpp).
NATIVE_FUNCS = [
    "clock_ticks",  # Reads the CPU counter directly.
    "io_init",  # Probes for flashcarts and SD card adapters by accessing PI registers.
    # Cartridge reads and writes through the PI (reads are used to read the ROM for savestates). The unlocked versions
    # are replaced too, as the locked ones are inlined into them.
    "pi_read",
    "pi_read_locked",
    "pi_write",
    "pi_write_locked",
]

# gz functions stubbed out, in addition to those found to use unsupported coprocessor 0 registers.
EXTRA_STUBS = [
    "zu_reset",  # Hard resets the console by reimplementing the boot process.
]

COP0_STATUS = 12


class Elf:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        if self.data[:4] != b"\x7fELF" or self.data[4] != 1 or self.data[5] != 2:
            raise ValueError(f"{path} isn't a 32-bit big endian ELF")
        shoff, = struct.unpack_from(">I", self.data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from(">HHH", self.data, 0x2E)
        self.sections = []
        for i in range(shnum):
            name, type_, flags, addr, offset, size, link, info, align, entsize = \
                struct.unpack_from(">IIIIIIIIII", self.data, shoff + i * shentsize)
            self.sections.append(dict(name_off=name, type=type_, flags=flags, addr=addr, offset=offset,
                                      size=size, link=link, entsize=entsize))
        strtab = self.sections[shstrndx]
        for s in self.sections:
            s["name"] = self._str(strtab, s["name_off"])

    def _str(self, strtab, off):
        start = strtab["offset"] + off
        return self.data[start:self.data.index(b"\0", start)].decode()

    def section(self, name):
        return next(s for s in self.sections if s["name"] == name)

    def symbols(self):
        symtab = self.section(".symtab")
        strtab = self.sections[symtab["link"]]
        for off in range(symtab["offset"], symtab["offset"] + symtab["size"], symtab["entsize"]):
            name, value, size, info, other, shndx = struct.unpack_from(">IIIBBH", self.data, off)
            yield self._str(strtab, name), value, size, info & 0xF, info >> 4, shndx


def read_liboot(path):
    """Reads the symbol definitions from gz's liboot linker script, keeping overlay-relative ones symbolic."""
    syms = {}
    for m in re.finditer(r"^(\w+)\s*=\s*([^;]+);", open(path).read(), re.MULTILINE):
        value = m.group(2).strip()
        rel = re.fullmatch(r"(\w+)\s*\+\s*(0x[0-9A-Fa-f]+)", value)
        if rel:
            syms[m.group(1)] = (rel.group(1), int(rel.group(2), 16))
        elif re.fullmatch(r"0x[0-9A-Fa-f]+", value):
            syms[m.group(1)] = int(value, 16)
    return syms


def read_sections(syms_text):
    return [(m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16))
            for m in re.finditer(r'name = "([^"]+)"\nrom = (0x\w+)\nvram = (0x\w+)\nsize = (0x\w+)', syms_text)]


def c_name(name):
    return FUNC_PREFIX + re.sub(r"[^A-Za-z0-9_]", "_", name)


def toml_str_list(items, indent="    "):
    return "[\n" + "".join(f'{indent}"{i}",\n' for i in items) + "]"


def main():
    args = sys.argv[1:]
    n64recomp = None
    if len(args) >= 2 and args[0] == "--recompile":
        n64recomp = os.path.abspath(args[1])
        args = args[2:]
    if len(args) < 1:
        print(__doc__)
        return 1
    gz_dir = args[0]
    build_dir = args[1] if len(args) > 1 else os.path.join(gz_dir, "bin-recomp", "gz", "oot-1.0")
    elf = Elf(os.path.join(build_dir, "gz.elf"))
    liboot = read_liboot(os.path.join(gz_dir, "lib", "liboot-1.0.a"))

    # Lay out the gz image, which starts at gz's load address and runs to the end of its loaded data.
    alloc = [s for s in elf.sections if s["flags"] & 0x2 and s["addr"] >= 0x80400000 and s["size"] != 0]
    image_vram = min(s["addr"] for s in alloc)
    progbits = [s for s in alloc if s["type"] == 1]
    nobits = [s for s in alloc if s["type"] == 8]
    image_end = max(s["addr"] + s["size"] for s in progbits)
    image = bytearray(image_end - image_vram)
    for s in progbits:
        image[s["addr"] - image_vram:s["addr"] - image_vram + s["size"]] = elf.data[s["offset"]:s["offset"] + s["size"]]
    bss_start = min(s["addr"] for s in nobits)
    bss_end = max(s["addr"] + s["size"] for s in nobits)
    text = elf.section(".text")
    text_index = elf.sections.index(text)

    # Collect gz's functions. Static functions whose names are used more than once get a unique suffix, and global
    # functions keep their names.
    raw_funcs = sorted({(value, size, name, bind) for name, value, size, type_, bind, shndx in elf.symbols()
                        if type_ == 2 and shndx == text_index and size != 0})
    name_counts = {}
    for _, _, name, _ in raw_funcs:
        name_counts[name] = name_counts.get(name, 0) + 1
    funcs = []
    funcs_by_name = {}
    for vram, size, name, bind in raw_funcs:
        unique = name_counts[name] == 1 or bind != 0
        out_name = c_name(name) if unique else c_name(f"{name}_{vram:08X}")
        funcs.append((out_name, vram, size))
        if unique:
            funcs_by_name[name] = (out_name, vram, size)

    def word_at(vram):
        off = vram - image_vram
        return struct.unpack_from(">I", image, off)[0]

    # Find instructions the recompiler doesn't support. Cache operations and traps are replaced with nops, and
    # functions using coprocessor 0 registers other than Status (only used for hardware access) are stubbed.
    stubs = set(c_name(n) for n in EXTRA_STUBS)
    nop_patches = []
    for name, vram, size in funcs:
        for addr in range(vram, vram + size, 4):
            w = word_at(addr)
            op = w >> 26
            if op == 0x2F or (op == 0 and (w & 0x3F) in (0x30, 0x31, 0x32, 0x33, 0x34, 0x36)):
                nop_patches.append((name, addr))
            elif op == 0x10 and ((w >> 21) & 0x1F) in (0, 1, 4, 5) and ((w >> 11) & 0x1F) != COP0_STATUS:
                stubs.add(name)
    # Functions that were removed from gz's build as unused don't need replacing.
    native = set(c_name(n) for n in NATIVE_FUNCS if n in funcs_by_name)
    stubs -= native
    nop_patches = [(n, a) for n, a in nop_patches if n not in stubs and n not in native]

    base_syms = open(BASE_SYMS).read()
    sections = read_sections(base_syms)

    def resolve_site(sym):
        value = liboot[sym]
        if isinstance(value, tuple):
            base = next(s for s in sections if s[0] == ".." + value[0])
            return base[2] + value[1]
        return value

    def rom_of(vram):
        for name, rom, sec_vram, size in sections:
            if sec_vram <= vram < sec_vram + size:
                return rom + vram - sec_vram
        raise ValueError(f"No section contains 0x{vram:08X}")

    # Assemble the hook instructions.
    writes = list(MEM_PATCH)
    for site_sym, instrs in HOOKS:
        site = resolve_site(site_sym)
        for i, instr in enumerate(instrs):
            if instr == "nop":
                word = 0
            elif isinstance(instr, int):
                word = instr
            else:
                word = 0x0C000000 | ((funcs_by_name[instr][1] >> 2) & 0x3FFFFFF)
            writes.append((site + i * 4, word))
    patched_vrams = {vram for vram, _ in writes}

    # Build the ROM for the recompiler.
    rom = bytearray(open(BASE_ROM, "rb").read())
    for vram, word in writes:
        struct.pack_into(">I", rom, rom_of(vram), word)
    gz_rom = (len(rom) + GZ_ROM_ALIGN - 1) // GZ_ROM_ALIGN * GZ_ROM_ALIGN
    rom += bytes(gz_rom - len(rom))
    rom += image

    # Write the symbols, dropping any relocations at the patched instructions so the recompiler uses the new ones.
    def keep_line(line):
        m = re.search(r"\{ type = \"R_MIPS_\w+\", vram = (0x\w+)", line)
        return not (m and int(m.group(1), 16) in patched_vrams)
    out_syms = "".join(l for l in base_syms.splitlines(keepends=True) if keep_line(l))
    text_rom = gz_rom + text["addr"] - image_vram
    out_syms += "\n[[section]]\n"
    out_syms += f'name = ".gz"\nrom = 0x{text_rom:08X}\nvram = 0x{text["addr"]:08X}\nsize = 0x{text["size"]:X}\n\n'
    out_syms += "functions = [\n"
    out_syms += "".join(f'    {{ name = "{n}", vram = 0x{v:08X}, size = 0x{s:X} }},\n' for n, v, s in funcs)
    out_syms += "]\n"

    # Write the recompiler config.
    base_config = tomllib.load(open(BASE_CONFIG, "rb"))
    base_patches = base_config.get("patches", {})
    start = funcs_by_name["_start"]
    ocarina_sync = resolve_site("z64_ocarina_sync")
    ocarina_sync_func = next(m.group(1) for m in
                             re.finditer(r'name = "([^"]+)", vram = (0x\w+), size = (0x\w+)', base_syms)
                             if 0 <= ocarina_sync - int(m.group(2), 16) < int(m.group(3), 16))

    config = "# Generated by oot/tools/gen_gz.py. Config for recompiling Ocarina of Time NTSC 1.0 (US) with gz.\n\n"
    config += "[input]\n"
    config += f'entrypoint = 0x{base_config["input"]["entrypoint"]:08X}\n'
    config += 'output_func_path = "../RecompiledFuncsGz"\n'
    config += 'relocatable_sections_path = "../overlays.us.rev0.txt"\n'
    config += 'symbols_file_path = "oot.us.rev0.gz.syms.toml"\n'
    config += 'rom_file_path = "oot.us.rev0.gz.rom.z64"\n\n'
    config += "[patches]\n"
    config += "stubs = " + toml_str_list(base_patches.get("stubs", []) + sorted(stubs)) + "\n"
    config += "ignored = " + toml_str_list(sorted(native)) + "\n\n"
    for patch in base_patches.get("instruction", []):
        config += f'[[patches.instruction]]\nfunc = "{patch["func"]}"\nvram = 0x{patch["vram"]:08X}\n' \
                  f'value = 0x{patch["value"]:08X}\n\n'
    for name, addr in nop_patches:
        config += f'[[patches.instruction]]\nfunc = "{name}"\nvram = 0x{addr:08X}\nvalue = 0x00000000\n\n'
    # gz's entry point replaces the game state's main function call, so call that directly unless gz was started.
    config += f'[[patches.hook]]\nfunc = "{start[0]}"\nbefore_vram = 0x{start[1]:08X}\n'
    config += f'text = "{{ extern int {GZ_ACTIVE_FUNC}(void); if (!{GZ_ACTIVE_FUNC}()) ' \
              '{ LOOKUP_FUNC(ctx->r25)(rdram, ctx); return; } }"\n\n'
    # gz removes a store in the ocarina update to sync ocarina playback when frame advancing. Keep it when gz
    # isn't started, so the game behaves as normal.
    orig_sync = struct.unpack_from(">I", open(BASE_ROM, "rb").read(), rom_of(ocarina_sync))[0]
    if orig_sync >> 26 != 0x2B:
        raise ValueError(f"Unexpected ocarina sync instruction 0x{orig_sync:08X}")
    rs, rt, imm = (orig_sync >> 21) & 0x1F, (orig_sync >> 16) & 0x1F, orig_sync & 0xFFFF
    config += f'[[patches.hook]]\nfunc = "{ocarina_sync_func}"\nbefore_vram = 0x{ocarina_sync:08X}\n'
    config += f'text = "{{ extern int {GZ_ACTIVE_FUNC}(void); if (!{GZ_ACTIVE_FUNC}()) ' \
              f'{{ MEM_W({imm if imm < 0x8000 else imm - 0x10000}, ctx->r{rs}) = ctx->r{rt}; }} }}"\n'

    # Write the image layout for the runtime.
    header = "// Generated by oot/tools/gen_gz.py.\n#pragma once\n\n"
    header += f"#define GZ_IMAGE_VRAM 0x{image_vram:08X}u\n"
    header += f"#define GZ_BSS_START 0x{bss_start:08X}u\n"
    header += f"#define GZ_BSS_END 0x{bss_end:08X}u\n"
    header += f"#define GZ_TEXT_ROM 0x{text_rom:08X}u\n"
    header += f"#define GZ_TEXT_VRAM 0x{text['addr']:08X}u\n"
    header += f"#define GZ_TEXT_SIZE 0x{text['size']:X}u\n"

    os.makedirs(OUT_DIR, exist_ok=True)
    outputs = {
        "oot.us.rev0.gz.rom.z64": bytes(rom),
        "oot.us.rev0.gz.syms.toml": out_syms.encode(),
        "us.rev0.gz.toml": config.encode(),
        "gz_image.bin": bytes(image),
        "gz_info.h": header.encode(),
    }
    changed = False
    for name, data in outputs.items():
        path = os.path.join(OUT_DIR, name)
        # Only rewrite changed files to avoid needless rebuilds.
        if not os.path.exists(path) or open(path, "rb").read() != data:
            with open(path, "wb") as f:
                f.write(data)
            changed = True

    print(f"gz: {len(funcs)} functions, {len(stubs)} stubbed, {len(nop_patches)} instructions removed, "
          f"image 0x{image_vram:08X}-0x{image_end:08X}, bss to 0x{bss_end:08X}")

    if n64recomp is not None:
        if changed or not os.path.exists(os.path.join(OOT_DIR, "RecompiledFuncsGz", "lookup.cpp")):
            print("gz: recompiling")
            result = subprocess.run([n64recomp, "us.rev0.gz.toml"], cwd=OUT_DIR, stdout=subprocess.DEVNULL)
            if result.returncode != 0:
                return result.returncode
        else:
            print("gz: recompiled code is up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
