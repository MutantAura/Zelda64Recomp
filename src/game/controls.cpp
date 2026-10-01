#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <deque>
#include <mutex>

#include "librecomp/helpers.hpp"
#include "recomp_input.h"
#include "zelda_config.h"
#include "zelda_game.h"
#include "ultramodern/ultramodern.hpp"

// Arrays that hold the mappings for every input for keyboard and controller respectively.
using input_mapping = std::array<recomp::InputField, recomp::bindings_per_input>;
using input_mapping_array = std::array<input_mapping, static_cast<size_t>(recomp::GameInput::COUNT)>;
static input_mapping_array keyboard_input_mappings{};
static input_mapping_array controller_input_mappings{};

// Make the button value array, which maps a button index to its bit field.
#define DEFINE_INPUT(name, value, readable) uint16_t(value##u),
static const std::array n64_button_values = {
    DEFINE_N64_BUTTON_INPUTS()
};
#undef DEFINE_INPUT

// Make the input name array.
#define DEFINE_INPUT(name, value, readable) readable,
static const std::vector<std::string> input_names = {
    DEFINE_ALL_INPUTS()
};
#undef DEFINE_INPUT

// Make the input enum name array.
#define DEFINE_INPUT(name, value, readable) #name,
static const std::vector<std::string> input_enum_names = {
    DEFINE_ALL_INPUTS()
};
#undef DEFINE_INPUT

size_t recomp::get_num_inputs() {
    return (size_t)GameInput::COUNT;
}

const std::string& recomp::get_input_name(GameInput input) {
    return input_names.at(static_cast<size_t>(input));
}

const std::string& recomp::get_input_enum_name(GameInput input) {
    return input_enum_names.at(static_cast<size_t>(input));
}

recomp::GameInput recomp::get_input_from_enum_name(const std::string_view enum_name) {
    auto find_it = std::find(input_enum_names.begin(), input_enum_names.end(), enum_name);
    if (find_it == input_enum_names.end()) {
        return recomp::GameInput::COUNT;
    }

    return static_cast<recomp::GameInput>(find_it - input_enum_names.begin());
}

// Due to an RmlUi limitation this can't be const. Ideally it would return a const reference or even just a straight up copy.
recomp::InputField& recomp::get_input_binding(GameInput input, size_t binding_index, recomp::InputDevice device) {
    input_mapping_array& device_mappings = (device == recomp::InputDevice::Controller) ?  controller_input_mappings : keyboard_input_mappings;
    input_mapping& cur_input_mapping = device_mappings.at(static_cast<size_t>(input));

    if (binding_index < cur_input_mapping.size()) {
        return cur_input_mapping[binding_index];
    }
    else {
        static recomp::InputField dummy_field = {};
        return dummy_field;
    }
}

void recomp::set_input_binding(recomp::GameInput input, size_t binding_index, recomp::InputDevice device, recomp::InputField value) {
    input_mapping_array& device_mappings = (device == recomp::InputDevice::Controller) ?  controller_input_mappings : keyboard_input_mappings;
    input_mapping& cur_input_mapping = device_mappings.at(static_cast<size_t>(input));

    if (binding_index < cur_input_mapping.size()) {
        cur_input_mapping[binding_index] = value;
    }
}

// Snaps a stick position to the nearest cardinal or diagonal direction if it's within the configured angle of it,
// like the notches of an N64 controller's gate. The distance from the center is preserved.
static void apply_stick_snapping(float* x, float* y) {
    int snap_degrees = zelda64::get_stick_snap_angle();
    if (snap_degrees <= 0) {
        return;
    }

    float magnitude = std::sqrt(*x * *x + *y * *y);
    if (magnitude == 0.0f) {
        return;
    }

    constexpr float notch_angle = std::numbers::pi_v<float> / 4.0f;
    float angle = std::atan2(*y, *x);
    float nearest_notch = std::round(angle / notch_angle) * notch_angle;

    if (std::fabs(angle - nearest_notch) <= snap_degrees * std::numbers::pi_v<float> / 180.0f) {
        float snapped_x = magnitude * std::cos(nearest_notch);
        float snapped_y = magnitude * std::sin(nearest_notch);
        // Remove floating point error so the other axis of a cardinal direction is exactly zero.
        *x = std::fabs(snapped_x) < 1e-6f ? 0.0f : snapped_x;
        *y = std::fabs(snapped_y) < 1e-6f ? 0.0f : snapped_y;
    }
}

// Converts a stick axis value relative to Ocarina of Time's deadzone to the input value that gives it, rounded so
// the raw value the game reads (which is truncated from this) is the nearest one.
static float oot_relative_to_input(float relative) {
    constexpr float game_deadzone = 7.0f;
    constexpr float raw_max = 127.0f;

    float magnitude = std::round(std::fabs(relative));
    if (magnitude == 0.0f) {
        return 0.0f;
    }
    float raw = std::min(magnitude + game_deadzone, raw_max);
    return std::copysign(std::min((raw + 0.5f) / raw_max, 1.0f), relative);
}

// Ocarina of Time ignores raw stick values up to 7 on each axis, and Link turns in place without moving while the
// stick's magnitude beyond that is under 20. This is the ESS range, raw values 8 to 27 when held straight in one
// direction. With the ESS range option set, that range is spread over the configured percentage of the stick's travel
// outside the deadzone, and the rest of the stick's range fits into the remaining travel. The result is converted
// relative to the game's deadzone so the ESS range is the same size in every direction.
static void apply_ess_range(float* x, float* y) {
    if (!zelda64::is_oot()) {
        return;
    }

    int percent = zelda64::get_ess_range();
    if (percent <= 0) {
        return;
    }

    float magnitude = std::sqrt(*x * *x + *y * *y);
    if (magnitude == 0.0f) {
        return;
    }

    constexpr float game_deadzone = 7.0f;
    constexpr float ess_start = 8.0f;
    constexpr float ess_end = 27.0f;
    constexpr float raw_max = 127.0f;

    float travel = std::min(magnitude, 1.0f);
    float ess_travel = percent / 100.0f;
    float raw_magnitude;
    if (travel <= ess_travel) {
        raw_magnitude = ess_start + (ess_end - ess_start) * (travel / ess_travel);
    }
    else {
        raw_magnitude = ess_end + (raw_max - ess_end) * ((travel - ess_travel) / (1.0f - ess_travel));
    }

    float relative_magnitude = raw_magnitude - game_deadzone;
    *x = oot_relative_to_input(relative_magnitude * *x / magnitude);
    *y = oot_relative_to_input(relative_magnitude * *y / magnitude);
}

// Reads the current state of the N64 controller from the bound inputs.
static void get_live_n64_input(uint16_t* buttons_out, float* x_out, float* y_out) {
    uint16_t cur_buttons = 0;
    float cur_x = 0.0f;
    float cur_y = 0.0f;

    if (!recomp::game_input_disabled()) {
        for (size_t i = 0; i < n64_button_values.size(); i++) {
            size_t input_index = (size_t)recomp::GameInput::N64_BUTTON_START + i;
            cur_buttons |= recomp::get_input_digital(keyboard_input_mappings[input_index]) ? n64_button_values[i] : 0;
            cur_buttons |= recomp::get_input_digital(controller_input_mappings[input_index]) ? n64_button_values[i] : 0;
        }

        float joystick_deadzone = recomp::get_joystick_deadzone() / 100.0f;

        float joystick_x = recomp::get_input_analog(controller_input_mappings[(size_t)recomp::GameInput::X_AXIS_POS])
                        - recomp::get_input_analog(controller_input_mappings[(size_t)recomp::GameInput::X_AXIS_NEG]);

        float joystick_y = recomp::get_input_analog(controller_input_mappings[(size_t)recomp::GameInput::Y_AXIS_POS])
                        - recomp::get_input_analog(controller_input_mappings[(size_t)recomp::GameInput::Y_AXIS_NEG]);

        recomp::apply_joystick_deadzone(joystick_x, joystick_y, &joystick_x, &joystick_y);
        apply_stick_snapping(&joystick_x, &joystick_y);
        apply_ess_range(&joystick_x, &joystick_y);

        cur_x = recomp::get_input_analog(keyboard_input_mappings[(size_t)recomp::GameInput::X_AXIS_POS])
                - recomp::get_input_analog(keyboard_input_mappings[(size_t)recomp::GameInput::X_AXIS_NEG]) + joystick_x;

        cur_y = recomp::get_input_analog(keyboard_input_mappings[(size_t)recomp::GameInput::Y_AXIS_POS])
                - recomp::get_input_analog(keyboard_input_mappings[(size_t)recomp::GameInput::Y_AXIS_NEG]) + joystick_y;
    }

    *buttons_out = cur_buttons;
    *x_out = std::clamp(cur_x, -1.0f, 1.0f);
    *y_out = std::clamp(cur_y, -1.0f, 1.0f);
}

// History of controller states used to apply the input lag option.
struct N64InputSample {
    std::chrono::steady_clock::time_point time;
    uint16_t buttons;
    float x;
    float y;
};

static std::mutex input_history_mutex;
static std::deque<N64InputSample> input_history;

void recomp::record_input_sample() {
    int lag_ms = zelda64::get_input_lag_ms();
    if (lag_ms <= 0) {
        std::lock_guard lock{ input_history_mutex };
        input_history.clear();
        return;
    }

    N64InputSample sample{ .time = std::chrono::steady_clock::now() };
    get_live_n64_input(&sample.buttons, &sample.x, &sample.y);

    std::lock_guard lock{ input_history_mutex };
    input_history.push_back(sample);

    // Drop samples that can no longer be requested, keeping the newest one from before the maximum lag.
    auto cutoff = sample.time - std::chrono::milliseconds(zelda64::max_input_lag_ms);
    while (input_history.size() > 1 && input_history[1].time <= cutoff) {
        input_history.pop_front();
    }
}

bool recomp::get_n64_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out) {
    if (controller_num != 0) {
        return false;
    }

    // If input lag is enabled, return the controller state from the configured amount of time ago.
    int lag_ms = zelda64::get_input_lag_ms();
    if (lag_ms > 0) {
        std::lock_guard lock{ input_history_mutex };
        if (!input_history.empty()) {
            auto target_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(lag_ms);
            // Use the oldest sample if the history doesn't go back far enough yet (e.g. the option was just enabled).
            const N64InputSample* sample = &input_history.front();
            for (auto it = input_history.rbegin(); it != input_history.rend(); ++it) {
                if (it->time <= target_time) {
                    sample = &*it;
                    break;
                }
            }
            *buttons_out = sample->buttons;
            *x_out = sample->x;
            *y_out = sample->y;
            return true;
        }
    }

    get_live_n64_input(buttons_out, x_out, y_out);
    return true;
}
