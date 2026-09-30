#include "patches.h"
#include "../../patches/misc_funcs.h"
#include "dma.h"
#include "main.h"
#include "segment_symbols.h"
#include "libu64/overlay.h"

RECOMP_DECLARE_EVENT(recomp_on_init());

// @recomp Patched to load the code segment in the recomp runtime.
RECOMP_PATCH void Main_ThreadEntry(void* arg) {
    DmaMgr_Init();

    // @recomp_event recomp_on_init(): Allow mods to initialize themselves once.
    recomp_on_init();

    // @recomp Load the code segment in the recomp runtime.
    recomp_load_overlays((uintptr_t)_codeSegmentRomStart, _codeSegmentStart, _codeSegmentRomEnd - _codeSegmentRomStart);

    DmaMgr_RequestSync(_codeSegmentStart, (uintptr_t)_codeSegmentRomStart, _codeSegmentRomEnd - _codeSegmentRomStart);
    bzero(_codeSegmentBssStart, _codeSegmentBssEnd - _codeSegmentBssStart);
    Main(arg);
}

extern u8 D_80121210;
extern u8 D_80121211;

// @recomp Patched to never load the 64DD code segment and always report that no 64DD is connected.
// The original function DMAs the n64dd segment and then probes the drive's registers directly.
RECOMP_PATCH void func_800AD410(void) {
    if (!D_80121210) {
        D_80121210 = true;
        D_80121211 = false;
    }
}

// @recomp Patched to load the overlay in the recomp runtime.
RECOMP_PATCH size_t Overlay_Load(uintptr_t vromStart, uintptr_t vromEnd, void* vramStart, void* vramEnd, void* allocatedRamAddr) {
    s32 size = vromEnd - vromStart;
    uintptr_t end;
    OverlayRelocationSection* ovlRelocs;

    // @recomp Load the overlay in the recomp runtime.
    recomp_load_overlays(vromStart, allocatedRamAddr, vromEnd - vromStart);

    end = (uintptr_t)allocatedRamAddr + size;

    DmaMgr_RequestSync(allocatedRamAddr, vromStart, size);

    ovlRelocs = (OverlayRelocationSection*)(end - ((s32*)end)[-1]);

    Overlay_Relocate(allocatedRamAddr, ovlRelocs, vramStart);

    if ((s32)ovlRelocs->bssSize != 0) {
        bzero((void*)end, (s32)ovlRelocs->bssSize);
    }

    size = (uintptr_t)vramEnd - (uintptr_t)vramStart;

    osWritebackDCache(allocatedRamAddr, size);
    osInvalICache(allocatedRamAddr, size);

    return size;
}
