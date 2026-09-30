#include "camera_patches.h"
#include "play_patches.h"
#include "pause.h"
#include "../../patches/input.h"

static bool prev_analog_cam_active = false;
static bool can_use_analog_cam = false;
static bool analog_cam_active = false;
static bool analog_cam_skip_once = false;

VecGeo analog_camera_pos = { .r = 66.0f, .pitch = 0, .yaw = 0 };

float analog_camera_yaw_vel = 0.0f;
float analog_camera_pitch_vel = 0.0f;

float analog_camera_x_sensitivity = 1500.0f;
float analog_camera_y_sensitivity = 500.0f;

static const float analog_cam_threshold = 0.1f;

static void update_analog_camera_params(Camera* camera) {
    // Check if the analog camera was usable in this frame.
    if (!can_use_analog_cam) {
        // It wasn't, so mark the analog cam as being off for this frame.
        analog_cam_active = false;
    }
    prev_analog_cam_active = analog_cam_active;
    if (!analog_cam_active) {
        // Use the same convention as the camera modes' eye VecGeo, which is relative to the at position.
        VecGeo at_eye_geo = OLib_Vec3fDiffToVecGeo(&camera->at, &camera->eye);
        analog_camera_pos.yaw = at_eye_geo.yaw;
        analog_camera_pos.pitch = at_eye_geo.pitch;
    }
}

static void update_analog_cam(Camera* c) {
    can_use_analog_cam = true;

    Player* player = GET_PLAYER(c->play);

    // Check if the player just started Z targeting and reset to auto cam if so.
    static bool prev_targeting_held = false;
    bool targeting_held = (player->stateFlags1 & (PLAYER_STATE1_Z_TARGETING | PLAYER_STATE1_PARALLEL)) || player->focusActor != NULL;
    if (targeting_held && !prev_targeting_held) {
        analog_cam_active = false;
    }

    // Enable analog cam if the right stick is held.
    float input_x, input_y;
    recomp_get_camera_inputs(&input_x, &input_y);

    if (fabsf(input_x) >= analog_cam_threshold || fabsf(input_y) >= analog_cam_threshold) {
        analog_cam_active = true;
    }

    if (analog_cam_skip_once) {
        analog_cam_active = false;
        analog_cam_skip_once = false;
    }

    // Record the Z targeting state.
    prev_targeting_held = targeting_held;

    if (analog_cam_active) {
        s32 inverted_x, inverted_y;
        recomp_get_analog_inverted_axes(&inverted_x, &inverted_y);

        if (inverted_x) {
            input_x = -input_x;
        }

        if (inverted_y) {
            input_y = -input_y;
        }

        analog_camera_yaw_vel = -input_x * analog_camera_x_sensitivity;
        analog_camera_pitch_vel = input_y * analog_camera_y_sensitivity;

        analog_camera_pos.pitch += analog_camera_pitch_vel;
        analog_camera_pos.yaw += analog_camera_yaw_vel;

        if (analog_camera_pos.pitch > 0x36B0) {
            analog_camera_pos.pitch = 0x36B0;
        }

        if (analog_camera_pos.pitch < -0x16D0) {
            analog_camera_pos.pitch = -0x16D0;
        }
    }
}

void analog_cam_apply(Camera* camera, VecGeo* eye_geo) {
    if (recomp_get_analog_cam_enabled()) {
        update_analog_cam(camera);

        if (analog_cam_active) {
            eye_geo->pitch = analog_camera_pos.pitch;
            eye_geo->yaw = analog_camera_pos.yaw;
        }
    }
}

void analog_cam_pre_play_update(PlayState* play) {
}

void analog_cam_post_play_update(PlayState* play) {
    Camera* active_cam = GET_ACTIVE_CAM(play);

    // Update parameters for the analog cam if the game is unpaused.
    if (!IS_PAUSED(&play->pauseCtx)) {
        update_analog_camera_params(active_cam);
        can_use_analog_cam = false;
    }

    // Make the player's movement relative to the analog camera's direction.
    if (analog_cam_active) {
        active_cam->inputDir.x = analog_camera_pos.pitch;
        active_cam->inputDir.y = analog_camera_pos.yaw + DEG_TO_BINANG(180);
    }
}

bool get_analog_cam_active() {
    return analog_cam_active;
}

void set_analog_cam_active(bool isActive) {
    analog_cam_active = isActive;
}

// Calling this will avoid analog cam taking over for the following game loop.
void skip_analog_cam_once() {
    analog_cam_skip_once = true;
}
