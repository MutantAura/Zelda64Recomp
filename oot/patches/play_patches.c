#include "patches.h"
#include "play_patches.h"
#include "debug_display.h"
#include "transition.h"
#include "../../patches/input.h"

extern Input* D_8012D1F8;
void Play_Update(PlayState* this);
void Play_Draw(PlayState* this);

RECOMP_DECLARE_EVENT(recomp_on_play_main(PlayState* play));
RECOMP_DECLARE_EVENT(recomp_on_play_update(PlayState* play));
RECOMP_DECLARE_EVENT(recomp_after_play_update(PlayState* play));

static void controls_play_update(PlayState* play) {
    // Apply the targeting mode setting (0: Switch, 1: Hold), matching the order of the options in the menu.
    gSaveContext.zTargetSetting = recomp_get_targeting_mode();
}

// @recomp Patched to add hooks for various added functionality.
RECOMP_PATCH void Play_Main(GameState* thisx) {
    PlayState* this = (PlayState*)thisx;

    // @recomp_event recomp_on_play_main(PlayState* play): Allow mods to execute code every frame.
    recomp_on_play_main(this);

    // @recomp
    controls_play_update(this);
    analog_cam_pre_play_update(this);

    D_8012D1F8 = &this->state.input[0];

    DebugDisplay_Init();

    camera_pre_play_update(this);

    // @recomp_event recomp_on_play_update(PlayState* play): Play_Update is about to be called.
    recomp_on_play_update(this);

    Play_Update(this);

    // @recomp_event recomp_after_play_update(PlayState* play): Play_Update was called.
    recomp_after_play_update(this);

    camera_post_play_update(this);
    analog_cam_post_play_update(this);
    autosave_post_play_update(this);

    Play_Draw(this);
}
