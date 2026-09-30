#include "patches.h"
#include "../../patches/input.h"

RECOMP_DECLARE_EVENT(recomp_before_first_person_aiming_update_event(PlayState* play, Player* this, bool in_free_look, RecompAimingOverideMode* recomp_aiming_override_mode));
RECOMP_DECLARE_EVENT(recomp_after_first_person_aiming_update_event(PlayState* play, Player* this, bool in_free_look));

s32 func_808334B4(Player* this);
s32 func_80836AB8(Player* this, s32 arg1);

// This flag is reset every frame by 'poll_inputs()'.
RecompAimingOverideMode recomp_aiming_override_mode = RECOMP_AIMING_OVERRIDE_OFF;

// @recomp Patched to add gyro and mouse aiming, right stick aiming with the analog camera, and aiming inversion.
// This is the first person view update, used for both free look (C-Up) and aiming items.
RECOMP_PATCH s16 func_8084ABD8(PlayState* play, Player* this, s32 arg2, s16 arg3) {
    s16 var_s0;
    Input* input = &play->state.input[0];

    // Checks if we're in free look (C-Up look around mode).
    bool in_free_look = (!func_8002DD78(this) && !func_808334B4(this) && !arg2);

    // Checking if any mods have disabled aiming with the left stick.
    recomp_before_first_person_aiming_update_event(play, this, in_free_look, &recomp_aiming_override_mode);

    // @recomp Get the aiming camera inversion state.
    s32 inverted_x, inverted_y;
    recomp_get_inverted_axes(&inverted_x, &inverted_y);

    // @recomp Get the analog camera input values if analog cam is enabled, or right-stick aiming is being forced.
    s32 analog_x = 0;
    s32 analog_y = 0;
    if (recomp_get_analog_cam_enabled() || recomp_aiming_override_mode == RECOMP_AIMING_OVERRIDE_FORCE_RIGHT_STICK) {
        float analog_x_float = 0.0f;
        float analog_y_float = 0.0f;
        recomp_get_camera_inputs(&analog_x_float, &analog_y_float);
        // Scale by 127 to match what ultramodern does, then clamp to 60 to match the game's handling.
        analog_x = (s32)(analog_x_float * 127.0f);
        analog_x = CLAMP(analog_x, -60, 60);
        analog_y = (s32)(analog_y_float * -127.0f);
        analog_y = CLAMP(analog_y, -60, 60);
    }

    if (in_free_look) {
        // @recomp Add in the analog camera Y input. Clamp to prevent moving the camera twice as fast if both sticks are held.
        s32 cam_input_y = analog_y;
        if (recomp_aiming_override_mode == RECOMP_AIMING_OVERRIDE_OFF) {
            cam_input_y += input->rel.stick_y;
        }
        var_s0 = CLAMP(cam_input_y, -61, 61) * 240;

        // @recomp Invert the Y axis accordingly (default is inverted, so negate if not inverted).
        if (!inverted_y) {
            var_s0 = -var_s0;
        }
        Math_SmoothStepToS(&this->actor.focus.rot.x, var_s0, 14, 4000, 30);

        // @recomp Add in the analog camera X input. Clamp to prevent moving the camera twice as fast if both sticks are held.
        s32 cam_input_x = analog_x;
        if (recomp_aiming_override_mode == RECOMP_AIMING_OVERRIDE_OFF) {
            cam_input_x += input->rel.stick_x;
        }
        var_s0 = CLAMP(cam_input_x, -61, 61) * -16;

        // @recomp Invert the X axis accordingly
        if (inverted_x) {
            var_s0 = -var_s0;
        }
        var_s0 = CLAMP(var_s0, -3000, 3000);
        this->actor.focus.rot.y += var_s0;
    }
    else {
        static float total_gyro_x, total_gyro_y;
        static float total_mouse_x, total_mouse_y;
        static int applied_aim_x, applied_aim_y;
        s32 limit;
        s16 temp3;

        float delta_gyro_x, delta_gyro_y;
        recomp_get_gyro_deltas(&delta_gyro_x, &delta_gyro_y);

        total_gyro_x += delta_gyro_x;
        total_gyro_y += delta_gyro_y;

        float delta_mouse_x, delta_mouse_y;
        recomp_get_mouse_deltas(&delta_mouse_x, &delta_mouse_y);

        total_mouse_x += delta_mouse_x;
        total_mouse_y += delta_mouse_y;

        // The gyro X-axis (tilt) corresponds to the camera X-axis (tilt).
        // The gyro Y-axis (left/right rotation) corresponds to the camera Y-axis (left/right rotation).
        // The mouse Y-axis (up/down movement) corresponds to the camera X-axis (tilt).
        // The mouse X-axis (left/right movement) corresponds to the camera Y-axis (left/right rotation).
        int target_aim_x = (int)(total_gyro_x * -3.0f + total_mouse_y * 20.0f);
        int target_aim_y = (int)(total_gyro_y * 3.0f  + total_mouse_x * -20.0f);

        // @recomp Invert the Y axis accordingly (default is inverted, so negate if not inverted).
        // Also add in the analog camera Y input. Clamp to prevent moving the camera twice as fast if both sticks are held.
        s32 cam_input_y = analog_y;
        if (recomp_aiming_override_mode == RECOMP_AIMING_OVERRIDE_OFF) {
            cam_input_y += input->rel.stick_y;
        }
        s32 stick_y = CLAMP(cam_input_y, -61, 61);

        if (!inverted_y) {
            stick_y = -stick_y;
        }

        limit = (this->stateFlags1 & PLAYER_STATE1_23) ? 3500 : 14000;
        temp3 = ((stick_y >= 0) ? 1 : -1) * (s32)((1.0f - Math_CosS(stick_y * 200)) * 1500.0f);
        this->actor.focus.rot.x += temp3 + (s32)(target_aim_x - applied_aim_x);
        applied_aim_x = target_aim_x;
        this->actor.focus.rot.x = CLAMP(this->actor.focus.rot.x, -limit, limit);

        // @recomp Invert the X axis accordingly. Also add in the analog camera X input.
        // Clamp to prevent moving the camera twice as fast if both sticks are held.
        s32 cam_input_x = analog_x;
        if (recomp_aiming_override_mode == RECOMP_AIMING_OVERRIDE_OFF) {
            cam_input_x += input->rel.stick_x;
        }
        s32 stick_x = CLAMP(cam_input_x, -61, 61);

        if (inverted_x) {
            stick_x = -stick_x;
        }

        limit = 19114;
        var_s0 = this->actor.focus.rot.y - this->actor.shape.rot.y;
        temp3 = ((stick_x >= 0) ? 1 : -1) * (s32)((1.0f - Math_CosS(stick_x * 200)) * -1500.0f);
        var_s0 += temp3 + (s32)(target_aim_y - applied_aim_y);
        applied_aim_y = target_aim_y;
        this->actor.focus.rot.y = CLAMP(var_s0, -limit, limit) + this->actor.shape.rot.y;
    }

    recomp_after_first_person_aiming_update_event(play, this, in_free_look);

    this->unk_6AE_rotFlags |= UNK6AE_ROT_FOCUS_Y;
    return func_80836AB8(this, (play->shootingGalleryStatus != 0) || func_8002DD78(this) || func_808334B4(this)) - arg3;
}
