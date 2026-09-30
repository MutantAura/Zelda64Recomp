#include "recomp.h"

// Stubs for libultra functions referenced by OoT's 64DD code, which the runtime doesn't implement.
// The 64DD is always reported as absent, so these are never expected to be called.

// s32 osEPiWriteIo(OSPiHandle* handle, uintptr_t devAddr, u32 data)
extern "C" void osEPiWriteIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = -1;
}

// OSPiHandle* osLeoDiskInit(void)
extern "C" void osLeoDiskInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}
