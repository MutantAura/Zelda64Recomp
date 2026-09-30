#include "ovl_patches.hpp"
#ifdef ZELDA64_GAME_OOT
#include "../../oot/RecompiledPatches/patches_bin.h"
#include "../../oot/RecompiledPatches/recomp_overlays.inl"
#define patches_bin oot_patches_bin
#else
#include "../../RecompiledPatches/patches_bin.h"
#include "../../RecompiledPatches/recomp_overlays.inl"
#define patches_bin mm_patches_bin
#endif

#include "librecomp/overlays.hpp"
#include "librecomp/game.hpp"

void zelda64::register_patches() {
    recomp::overlays::register_patches(patches_bin, sizeof(patches_bin), section_table, ARRLEN(section_table));
    recomp::overlays::register_base_exports(export_table);
    recomp::overlays::register_base_events(event_names);
    recomp::overlays::register_manual_patch_symbols(manual_patch_symbols);
}
