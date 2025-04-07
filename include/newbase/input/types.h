#pragma once

#include <entt/entt.hpp>
#include <set>

namespace nb {

enum class gamepad_button
{ 
    BTN_NONE,

    // face buttons
    BTN_SOUTH,  // xbox a
    BTN_EAST,   // xbox b
    BTN_WEST,   // xbox x
    BTN_NORTH,  // xbox y

    // bumpers and triggers
    BTN_BUMPER_L,
    BTN_BUMPER_R,
    BTN_TRIGGER_L,
    BTN_TRIGGER_R,

    // analog stick buttons
    BTN_LS,  // xbox y
    BTN_RS,  // xbox y
    
    // dpad as buttons
    BTN_DPAD_DOWN,
    BTN_DPAD_RIGHT,
    BTN_DPAD_LEFT,
    BTN_DPAD_UP,
    
    // "center" buttons
    BTN_START,  // plus
    BTN_SELECT, // minus
    BTN_META   // home, guide, etc
};

enum class gamepad_axis
{
    GPA_AXIS_LEFT_X,
    GPA_AXIS_LEFT_Y,
    GPA_AXIS_RIGHT_X,
    GPA_AXIS_RIGHT_Y,
    GPA_AXIS_TRIGGER_X,
    GPA_AXIS_TRIGGER_Y
};

enum class input_direction
{
    IDIR_UP,
    IDIR_DOWN,
    IDIR_LEFT,
    IDIR_RIGHT,
    IDIR_FORWARD,
    IDIR_BACKWARD
};

enum class input_axis
{
    IAXIS_X,
    IAXIS_Y,
    IAXIS_Z
};

struct gamepad_config
{
    float left_analog_deadzone {0.15f};
    float right_analog_deadzone {0.15f};
    float trigger_threshold {0.3f};
};

struct input_action
{
    entt::hashed_string id {};
    std::set<gamepad_button> gp_btns {};
    std::set<gamepad_axis> gp_axii {};
    std::set<uint32_t> kbd_scancodes {};

    bool directional {false};   // a directional action can map inputs to directions in mutiple axii
                                // the resulting event will combine all input states into a resulting direction vector
    std::unordered_map<gamepad_button, input_direction> dir_gp_btns;
    std::unordered_map<gamepad_axis, input_axis> dir_gp_axii;
    std::unordered_map<uint32_t, input_direction> dir_kbd_scancodes;
};

}