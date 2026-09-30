#include "patches.h"
#include "graph_patches.h"
#include "play_patches.h"

void draw_autosave_icon(GraphicsContext* gfxCtx);

// Per-frame graphics hook, called after the game state has been updated and before the frame's display lists are finalized.
void graph_pre_submit_hook(GraphicsContext* gfxCtx, GameState* gameState) {
    draw_autosave_icon(gfxCtx);
}

// Camera interpolation hooks, called around Play_Update.
void camera_pre_play_update(PlayState* play) {
}

void camera_post_play_update(PlayState* play) {
}
