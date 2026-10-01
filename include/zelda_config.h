#ifndef __ZELDA_CONFIG_H__
#define __ZELDA_CONFIG_H__

#include <filesystem>
#include <string_view>
#include "ultramodern/config.hpp"
#include "recomp_input.h"

namespace zelda64 {
    constexpr std::u8string_view program_id = u8"Zelda64Recompiled";
    constexpr std::string_view program_name = "Zelda 64: Recompiled";
#ifdef ZELDA64_GAME_OOT
    constexpr const char* window_title = "Ocarina of Time: Recompiled";
#else
    constexpr const char* window_title = "Zelda 64: Recompiled";
#endif

    // TODO: Move loading configs to the runtime once we have a way to allow per-project customization.
    void load_config();
    void save_config();
    
    void reset_input_bindings();
    void reset_cont_input_bindings();
    void reset_kb_input_bindings();
    void reset_single_input_binding(recomp::InputDevice device, recomp::GameInput input);

    std::filesystem::path get_app_folder_path();
    
    bool get_debug_mode_enabled();
    void set_debug_mode_enabled(bool enabled);
    
    enum class AutosaveMode {
        On,
        Off,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::AutosaveMode, {
        {zelda64::AutosaveMode::On, "On"},
        {zelda64::AutosaveMode::Off, "Off"}
    });

    enum class TargetingMode {
        Switch,
        Hold,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::TargetingMode, {
        {zelda64::TargetingMode::Switch, "Switch"},
        {zelda64::TargetingMode::Hold, "Hold"}
    });

    TargetingMode get_targeting_mode();
    void set_targeting_mode(TargetingMode mode);

    enum class CameraInvertMode {
        InvertNone,
        InvertX,
        InvertY,
        InvertBoth,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::CameraInvertMode, {
        {zelda64::CameraInvertMode::InvertNone, "InvertNone"},
        {zelda64::CameraInvertMode::InvertX, "InvertX"},
        {zelda64::CameraInvertMode::InvertY, "InvertY"},
        {zelda64::CameraInvertMode::InvertBoth, "InvertBoth"}
    });

    CameraInvertMode get_camera_invert_mode();
    void set_camera_invert_mode(CameraInvertMode mode);

    CameraInvertMode get_analog_camera_invert_mode();
    void set_analog_camera_invert_mode(CameraInvertMode mode);

    enum class AnalogCamMode {
        On,
        Off,
		OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::AnalogCamMode, {
        {zelda64::AnalogCamMode::On, "On"},
        {zelda64::AnalogCamMode::Off, "Off"}
    });

    AutosaveMode get_autosave_mode();
    void set_autosave_mode(AutosaveMode mode);

    AnalogCamMode get_analog_cam_mode();
    void set_analog_cam_mode(AnalogCamMode mode);

    // Emulated console lag when opening and closing the pause menu, in VIs (1/60th of a second).
    constexpr int max_pause_lag_vis = 30;
    int get_pause_lag_vis();
    void set_pause_lag_vis(int vis);
    int get_unpause_lag_vis();
    void set_unpause_lag_vis(int vis);

    // Snaps the control stick to the nearest cardinal or diagonal when within this many degrees of it.
    constexpr int max_stick_snap_angle = 22;
    int get_stick_snap_angle();
    void set_stick_snap_angle(int degrees);

    // Ocarina of Time only. The percentage of the control stick's travel outside the deadzone that's used for the ESS
    // range, the raw stick values from 8 to 27 where Link turns in place without moving. 0 disables it.
    constexpr int max_ess_range = 50;
    int get_ess_range();
    void set_ess_range(int percent);

    // Artificial delay applied to controller input, in milliseconds.
    constexpr int max_input_lag_ms = 200;
    int get_input_lag_ms();
    void set_input_lag_ms(int ms);

    void open_quit_game_prompt();
};

#endif
