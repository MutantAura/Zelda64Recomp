#include "patches.h"
#include "graph_patches.h"
#include "buffers.h"
#include "sys_cfb.h"
#include "game.h"
#include "audio.h"
#include "../../patches/misc_funcs.h"

// 10 times bigger than the game's normal buffers.
typedef struct {
    Gfx polyOpaBuffer[0x17E0 * 10];
    Gfx polyXluBuffer[0x800 * 10];
    Gfx overlayBuffer[0x400 * 10];
    Gfx workBuffer[0x80 * 10];
} BiggerGfxPool;

BiggerGfxPool gBiggerGfxPools[2];

// The number of VIs to wait for the current frame. This is the game's update rate plus any extra VIs requested by patches.
s32 recomp_frame_vis = 3;
// Extra VIs to wait for the current frame, which can be set by patches to match console performance in specific cases.
s32 recomp_extra_vis = 0;
// Lag VIs to stall for after the current frame is displayed, which delays the next input poll like a console lag frame.
s32 recomp_lag_vis = 0;

void recomp_crash(const char* err) {
    recomp_printf("%s\n", err);
    // TODO open a message box instead of a hard crash
    *(volatile int*)0 = 0;
}

DECLARE_FUNC(s32, recomp_gz_active);
DECLARE_FUNC(void, recomp_gz_disp_hook, TwoHeadGfxArena* arena, void* start, size_t size);
DECLARE_FUNC(void, recomp_gz_input_hook);
DECLARE_FUNC(void, recomp_gz_ocarina_update_hook);

// gz (the practice ROM) hooks into THGA_Init to track the display list buffers, which it uses for frame advance.
static void init_gfx_arena(TwoHeadGfxArena* arena, void* start, size_t size) {
    if (recomp_gz_active()) {
        recomp_gz_disp_hook(arena, start, size);
    }
    else {
        THGA_Init(arena, start, size);
    }
}

// @recomp Use the bigger gfx pools and enable RT64 extended GBI mode.
RECOMP_PATCH void Graph_InitTHGA(GraphicsContext* gfxCtx) {
    GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];
    BiggerGfxPool* bigger_pool = &gBiggerGfxPools[gfxCtx->gfxPoolIdx & 1];

    pool->headMagic = GFXPOOL_HEAD_MAGIC;
    pool->tailMagic = GFXPOOL_TAIL_MAGIC;

    // @recomp gz's frame advance redraws the previous frame by copying the game's gfx pool, so use the game's own
    // buffers when gz is running.
    if (recomp_gz_active()) {
        init_gfx_arena(&gfxCtx->polyOpa, pool->polyOpaBuffer, sizeof(pool->polyOpaBuffer));
        init_gfx_arena(&gfxCtx->polyXlu, pool->polyXluBuffer, sizeof(pool->polyXluBuffer));
        init_gfx_arena(&gfxCtx->overlay, pool->overlayBuffer, sizeof(pool->overlayBuffer));
        init_gfx_arena(&gfxCtx->work, pool->workBuffer, sizeof(pool->workBuffer));

        gfxCtx->polyOpaBuffer = pool->polyOpaBuffer;
        gfxCtx->polyXluBuffer = pool->polyXluBuffer;
        gfxCtx->overlayBuffer = pool->overlayBuffer;
        gfxCtx->workBuffer = pool->workBuffer;
    }
    else {
        init_gfx_arena(&gfxCtx->polyOpa, bigger_pool->polyOpaBuffer, sizeof(bigger_pool->polyOpaBuffer));
        init_gfx_arena(&gfxCtx->polyXlu, bigger_pool->polyXluBuffer, sizeof(bigger_pool->polyXluBuffer));
        init_gfx_arena(&gfxCtx->overlay, bigger_pool->overlayBuffer, sizeof(bigger_pool->overlayBuffer));
        init_gfx_arena(&gfxCtx->work, bigger_pool->workBuffer, sizeof(bigger_pool->workBuffer));

        gfxCtx->polyOpaBuffer = bigger_pool->polyOpaBuffer;
        gfxCtx->polyXluBuffer = bigger_pool->polyXluBuffer;
        gfxCtx->overlayBuffer = bigger_pool->overlayBuffer;
        gfxCtx->workBuffer = bigger_pool->workBuffer;
    }

    gfxCtx->curFrameBuffer = SysCfb_GetFbPtr(gfxCtx->fbIdx % 2);
    gfxCtx->unk_014 = 0;

    // @recomp Enable RT64 extended GBI mode and extended rdram at the start of the task, as the work buffer runs first.
    OPEN_DISPS(gfxCtx, "", 0);
    gEXEnable(WORK_DISP++);
    gEXSetRDRAMExtended(WORK_DISP++, 1);
    CLOSE_DISPS(gfxCtx, "", 0);
}

// @recomp Modified to report errors instead of skipping frames, and to send the framerate to RT64.
RECOMP_PATCH void Graph_Update(GraphicsContext* gfxCtx, GameState* gameState) {
    gameState->inPreNMIState = false;
    Graph_InitTHGA(gfxCtx);

    // @recomp gz hooks the input update and the game state update (the latter in the recompiled game code).
    if (recomp_gz_active()) {
        recomp_gz_input_hook();
    }
    else {
        GameState_ReqPadData(gameState);
    }
    GameState_Update(gameState);

    // @recomp Determine the number of VIs for this frame, including any extra ones requested by patches.
    recomp_frame_vis = R_UPDATE_RATE + recomp_extra_vis;
    if (recomp_frame_vis <= 0) {
        recomp_frame_vis = 1;
    }
    recomp_extra_vis = 0;

    OPEN_DISPS(gfxCtx, "../graph.c", 999);

    // @recomp Send the current framerate to RT64.
    gEXSetRefreshRate(WORK_DISP++, 60 / recomp_frame_vis);

    // @recomp Run any per-frame graphics hooks from other patches (e.g. interpolation fixups).
    graph_pre_submit_hook(gfxCtx, gameState);

    gSPBranchList(WORK_DISP++, gfxCtx->polyOpaBuffer);
    gSPBranchList(POLY_OPA_DISP++, gfxCtx->polyXluBuffer);
    gSPBranchList(POLY_XLU_DISP++, gfxCtx->overlayBuffer);
    gDPPipeSync(OVERLAY_DISP++);
    gDPFullSync(OVERLAY_DISP++);
    gSPEndDisplayList(OVERLAY_DISP++);

    CLOSE_DISPS(gfxCtx, "../graph.c", 1028);

    // @recomp Patch all error conditions to print to console and crash the application.
    {
        GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];

        if (pool->headMagic != GFXPOOL_HEAD_MAGIC) {
            recomp_crash("GfxPool headMagic integrity check failed!");
        }
        if (pool->tailMagic != GFXPOOL_TAIL_MAGIC) {
            recomp_crash("GfxPool tailMagic integrity check failed!");
        }
    }

    if (THGA_IsCrash(&gfxCtx->polyOpa)) {
        recomp_crash("gfxCtx->polyOpa overflow!");
    }
    if (THGA_IsCrash(&gfxCtx->polyXlu)) {
        recomp_crash("gfxCtx->polyXlu overflow!");
    }
    if (THGA_IsCrash(&gfxCtx->overlay)) {
        recomp_crash("gfxCtx->overlay overflow!");
    }

    Graph_TaskSet00(gfxCtx);
    gfxCtx->gfxPoolIdx++;
    gfxCtx->fbIdx++;

    // @recomp gz hooks the audio update to sync the ocarina when frame advancing.
    if (recomp_gz_active()) {
        recomp_gz_ocarina_update_hook();
    }
    else {
        Audio_Update();
    }

    {
        OSTime timeNow = osGetTime();

        gRSPGfxTimeTotal = gRSPGfxTimeAcc;
        gRSPAudioTimeTotal = gRSPAudioTimeAcc;
        gRDPTimeTotal = gRDPTimeAcc;
        gRSPGfxTimeAcc = 0;
        gRSPAudioTimeAcc = 0;
        gRDPTimeAcc = 0;

        if (sGraphPrevUpdateEndTime != 0) {
            gGraphUpdatePeriod = timeNow - sGraphPrevUpdateEndTime;
        }
        sGraphPrevUpdateEndTime = timeNow;
    }
}
