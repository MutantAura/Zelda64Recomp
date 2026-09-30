#include "patches.h"
#include "graph_patches.h"
#include "kaleido_manager.h"
#include "letterbox.h"
#include "regs.h"
#include "play_state.h"
#include "../../patches/patch_helpers.h"

// Emulates the lag frames that occur on console when opening and closing the pause menu. On console, the frame that
// initializes the pause menu and the frame that resumes gameplay (which reloads every object from the cartridge) take
// several VIs longer than normal. Inputs made during that time are read on the following frame, which is what allows
// pause buffering. The recompilation runs these frames instantly, so this adds the lag back based on the user's settings.

DECLARE_FUNC(s32, recomp_get_pause_lag_vis);
DECLARE_FUNC(s32, recomp_get_unpause_lag_vis);

extern void (*sKaleidoScopeUpdateFunc)(PlayState* play);

// @recomp Patched to add emulated console lag when opening and closing the pause menu.
RECOMP_PATCH void KaleidoScopeCall_Update(PlayState* play) {
    KaleidoMgrOverlay* kaleidoScopeOvl = &gKaleidoMgrOverlayTable[KALEIDO_OVL_KALEIDO_SCOPE];
    PauseContext* pauseCtx = &play->pauseCtx;

    if (IS_PAUSED(&play->pauseCtx)) {
        if (pauseCtx->state == PAUSE_STATE_WAIT_LETTERBOX) {
            if (Letterbox_GetSize() == 0) {
                R_PAUSE_BG_PRERENDER_STATE = PAUSE_BG_PRERENDER_SETUP;
                pauseCtx->mainState = PAUSE_MAIN_STATE_IDLE;
                pauseCtx->savePromptState = PAUSE_SAVE_PROMPT_STATE_APPEARING;
                pauseCtx->state = (pauseCtx->state & 0xFFFF) + 1; // PAUSE_STATE_WAIT_BG_PRERENDER
            }
        } else if (pauseCtx->state == PAUSE_STATE_GAME_OVER_START) {
            R_PAUSE_BG_PRERENDER_STATE = PAUSE_BG_PRERENDER_SETUP;
            pauseCtx->mainState = PAUSE_MAIN_STATE_IDLE;
            pauseCtx->savePromptState = PAUSE_SAVE_PROMPT_STATE_APPEARING; // copied from pause menu, not needed here
            pauseCtx->state = (pauseCtx->state & 0xFFFF) + 1;              // PAUSE_STATE_GAME_OVER_WAIT_BG_PRERENDER
        } else if ((pauseCtx->state == PAUSE_STATE_WAIT_BG_PRERENDER) ||
                   (pauseCtx->state == PAUSE_STATE_GAME_OVER_WAIT_BG_PRERENDER)) {
            if (R_PAUSE_BG_PRERENDER_STATE >= PAUSE_BG_PRERENDER_READY) {
                pauseCtx->state++; // PAUSE_STATE_INIT or PAUSE_STATE_GAME_OVER_INIT
            }
        } else if (pauseCtx->state != PAUSE_STATE_OFF) {
            if (gKaleidoMgrCurOvl != kaleidoScopeOvl) {
                if (gKaleidoMgrCurOvl != NULL) {
                    KaleidoManager_ClearOvl(gKaleidoMgrCurOvl);
                }

                KaleidoManager_LoadOvl(kaleidoScopeOvl);
            }

            if (gKaleidoMgrCurOvl == kaleidoScopeOvl) {
                // @recomp Track the state before the update to detect the pause menu opening and gameplay resuming.
                s32 prev_state = pauseCtx->state;

                sKaleidoScopeUpdateFunc(play);

                // @recomp The pause menu finished its initialization this frame, so add the pause lag.
                if (prev_state == PAUSE_STATE_INIT) {
                    recomp_lag_vis += recomp_get_pause_lag_vis();
                }

                if (!IS_PAUSED(&play->pauseCtx)) {
                    KaleidoManager_ClearOvl(kaleidoScopeOvl);
                    KaleidoScopeCall_LoadPlayer();

                    // @recomp Gameplay resumed this frame, so add the unpause lag.
                    if (prev_state == PAUSE_STATE_RESUME_GAMEPLAY) {
                        recomp_lag_vis += recomp_get_unpause_lag_vis();
                    }
                }
            }
        }
    }
}
