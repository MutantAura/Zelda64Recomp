#include "patches.h"
#include "play_patches.h"
#include "graph_patches.h"
#include "sram.h"
#include "message.h"
#include "pause.h"
#include "transition.h"
#include "gfx_setupdl.h"
#include "../../patches/misc_funcs.h"

void Play_SaveSceneFlags(PlayState* this);

int autosave_icon_counter = 0;

#define AUTOSAVE_ICON_FADE_OUT_FRAMES 5
#define AUTOSAVE_ICON_FADE_IN_FRAMES 5
#define AUTOSAVE_ICON_SHOW_FRAMES 30
#define AUTOSAVE_ICON_TOTAL_FRAMES (AUTOSAVE_ICON_FADE_IN_FRAMES + AUTOSAVE_ICON_SHOW_FRAMES + AUTOSAVE_ICON_FADE_OUT_FRAMES)

#define AUTOSAVE_ICON_WIDTH 24
#define AUTOSAVE_ICON_HEIGHT 16
#define AUTOSAVE_ICON_DRAW_WIDTH (AUTOSAVE_ICON_WIDTH * 3 / 4)
#define AUTOSAVE_ICON_DRAW_HEIGHT (AUTOSAVE_ICON_HEIGHT * 3 / 4)
#define AUTOSAVE_ICON_X (SCREEN_WIDTH - AUTOSAVE_ICON_DRAW_WIDTH - 4)
#define AUTOSAVE_ICON_Y (SCREEN_HEIGHT - AUTOSAVE_ICON_DRAW_HEIGHT - 4)

// Shared with the Majora's Mask patches.
INCBIN(autosave_icon, "../../patches/autosave.rgba32.bin");

static Gfx* GfxEx_DrawRect_DropShadow(Gfx* gfx, s16 rectLeft, s16 rectTop, s16 rectWidth, s16 rectHeight, u16 dsdx, u16 dtdy,
                                      s16 r, s16 g, s16 b, s16 a, u16 origin) {
    s16 dropShadowAlpha = a;

    if (a > 100) {
        dropShadowAlpha = 100;
    }

    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, dropShadowAlpha);
    gEXTextureRectangle(gfx++, origin, origin, (rectLeft + 2) * 4, (rectTop + 2) * 4, (rectLeft + rectWidth + 2) * 4,
                        (rectTop + rectHeight + 2) * 4, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);

    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);

    gEXTextureRectangle(gfx++, origin, origin,  rectLeft * 4, rectTop * 4, (rectLeft + rectWidth) * 4, (rectTop + rectHeight) * 4,
                        G_TX_RENDERTILE, 0, 0, dsdx, dtdy);

    return gfx;
}

void draw_autosave_icon(GraphicsContext* gfxCtx) {
    s32 alpha = 0;
    if (autosave_icon_counter > (AUTOSAVE_ICON_SHOW_FRAMES + AUTOSAVE_ICON_FADE_OUT_FRAMES)) {
        alpha = (255 * (AUTOSAVE_ICON_FADE_IN_FRAMES - (autosave_icon_counter - AUTOSAVE_ICON_SHOW_FRAMES - AUTOSAVE_ICON_FADE_OUT_FRAMES))) / AUTOSAVE_ICON_FADE_IN_FRAMES;
    }
    else if (autosave_icon_counter > AUTOSAVE_ICON_FADE_OUT_FRAMES) {
        alpha = 255;
    }
    else if (autosave_icon_counter > 0) {
        alpha = (255 * autosave_icon_counter) / AUTOSAVE_ICON_FADE_OUT_FRAMES;
    }

    if (autosave_icon_counter > 0) {
        autosave_icon_counter--;
    }

    if (alpha != 0) {
        Gfx_SetupDL_39Overlay(gfxCtx);

        OPEN_DISPS(gfxCtx, "", 0);

        gEXForceUpscale2D(OVERLAY_DISP++, 1);
        gDPLoadTextureBlock(OVERLAY_DISP++, autosave_icon, G_IM_FMT_RGBA, G_IM_SIZ_32b, AUTOSAVE_ICON_WIDTH, AUTOSAVE_ICON_HEIGHT, 0,
            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

        gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
        OVERLAY_DISP = GfxEx_DrawRect_DropShadow(OVERLAY_DISP, AUTOSAVE_ICON_X - SCREEN_WIDTH, AUTOSAVE_ICON_Y, AUTOSAVE_ICON_DRAW_WIDTH, AUTOSAVE_ICON_DRAW_HEIGHT,
            (s32)(1024.0f * AUTOSAVE_ICON_WIDTH / AUTOSAVE_ICON_DRAW_WIDTH), (s32)(1024.0f * AUTOSAVE_ICON_HEIGHT / AUTOSAVE_ICON_DRAW_HEIGHT),
            255, 255, 255, alpha, G_EX_ORIGIN_RIGHT);
        gEXForceUpscale2D(OVERLAY_DISP++, 0);

        CLOSE_DISPS(gfxCtx, "", 0);
    }
}

RECOMP_DECLARE_EVENT(recomp_on_autosave(PlayState* play));
RECOMP_DECLARE_EVENT(recomp_after_autosave(PlayState* play));

// @recomp_export void recomp_do_autosave(PlayState* play): Saves the game the same way as saving from the pause menu does.
RECOMP_EXPORT void recomp_do_autosave(PlayState* play) {
    // @recomp_event recomp_on_autosave(PlayState* play): Autosave triggered.
    recomp_on_autosave(play);

    Play_SaveSceneFlags(play);
    gSaveContext.save.info.playerData.savedSceneId = play->sceneId;
    Sram_WriteSave(&play->sramCtx);

    // @recomp_event recomp_after_autosave(PlayState* play): Autosave finished.
    recomp_after_autosave(play);
}

RECOMP_EXPORT void recomp_show_autosave_icon() {
    autosave_icon_counter = AUTOSAVE_ICON_TOTAL_FRAMES;
}

RECOMP_EXPORT u32 recomp_autosave_interval() {
    return 2 * 60 * 1000;
}

#define MIN_FRAMES_SINCE_READY 20
OSTime last_autosave_time = 0;
u32 extra_autosave_delay_milliseconds = 0;

RECOMP_EXPORT void recomp_reset_autosave_timer() {
    last_autosave_time = osGetTime();
    extra_autosave_delay_milliseconds = 0;
}

RECOMP_EXPORT void recomp_reset_autosave_timer_slow() {
    // Set the most recent autosave time in the future to give extra time before an autosave triggers.
    last_autosave_time = osGetTime();
    extra_autosave_delay_milliseconds = 2 * 60 * 1000;
}

void autosave_post_play_update(PlayState* play) {
    static int frames_since_autosave_ready = 0;
    Player* player = GET_PLAYER(play);

    if (recomp_get_autosave_enabled()) {
        OSTime time_now = osGetTime();

        // Check the following conditions for autosave safety:
        // * A save file is loaded and the game is in normal gameplay.
        // * The HUD is in a normal state.
        // * No scene transition is happening.
        // * No message is on screen.
        // * The game is not paused.
        // * No cutscene is running.
        // * The game is not in cutscene mode.
        // * No timer or minigame is active.
        // * The player is alive.
        if (gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2 &&
            gSaveContext.gameMode == GAMEMODE_NORMAL &&
            gSaveContext.hudVisibilityMode == HUD_VISIBILITY_ALL &&
            play->transitionTrigger == TRANS_TRIGGER_OFF &&
            play->transitionMode == TRANS_MODE_OFF &&
            play->msgCtx.msgMode == MSGMODE_NONE &&
            !IS_PAUSED(&play->pauseCtx) &&
            gSaveContext.save.cutsceneIndex < 0xFFF0 &&
            !Play_InCsMode(play) &&
            gSaveContext.timerState == TIMER_STATE_OFF &&
            gSaveContext.subTimerState == SUBTIMER_STATE_OFF &&
            gSaveContext.minigameState == 0 &&
            player != NULL &&
            gSaveContext.save.info.playerData.health > 0 &&
            !(player->stateFlags1 & PLAYER_STATE1_DEAD)
        ) {
            frames_since_autosave_ready++;
        }
        else {
            frames_since_autosave_ready = 0;
        }

        // Check that autosaving has been ready for long enough and that enough time has passed since the previous autosave.
        if (frames_since_autosave_ready >= MIN_FRAMES_SINCE_READY &&
            time_now - last_autosave_time > (OS_USEC_TO_CYCLES(1000 * (u64)(recomp_autosave_interval() + extra_autosave_delay_milliseconds)))
        ) {
            recomp_do_autosave(play);
            recomp_show_autosave_icon();
            recomp_reset_autosave_timer();
        }
    }
    else {
        // Update the last autosave time to the current time to prevent autosaving immediately if autosaves are turned back on.
        recomp_reset_autosave_timer();
    }
}
