#include "patches.h"
#include "play_state.h"
#include "pause.h"

void KaleidoScope_UpdateCursorVtx(PlayState* play);

// The pause menu cursor's quads are positioned by KaleidoScope_UpdateCursorVtx, which runs during the update of the
// frame after the cursor is drawn, on a vertex buffer that the draw step reallocated and zeroed. On console the cursor
// still appears, as the RSP draws each frame while the CPU runs the next frame's update, so the vertices are filled in
// before the RSP reads them. The recomp draws each frame when it's submitted, which would leave the cursor's quads
// empty. This positions the quads before the cursor is drawn instead.
//
// Called by a hook at the start of KaleidoScope_DrawCursor (see oot/us.rev0.toml). The conditions match the ones for
// drawing the cursor.
void kaleido_draw_cursor_hook(PlayState* play, u16 pageIndex) {
    PauseContext* pauseCtx = &play->pauseCtx;

    if (((((u32)pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) ||
          (pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE_CURSOR_ON_SONG)) &&
         (pauseCtx->state == PAUSE_STATE_MAIN)) ||
        ((pauseCtx->pageIndex == PAUSE_QUEST) &&
         ((pauseCtx->mainState < PAUSE_MAIN_STATE_3) || (pauseCtx->mainState == PAUSE_MAIN_STATE_SONG_PROMPT) ||
          (pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE_CURSOR_ON_SONG)))) {
        if (pauseCtx->pageIndex == pageIndex) {
            KaleidoScope_UpdateCursorVtx(play);
        }
    }
}
