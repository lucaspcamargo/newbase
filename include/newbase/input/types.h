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
    BTN_LS,
    BTN_RS,
    
    // dpad as buttons
    BTN_DPAD_DOWN,
    BTN_DPAD_RIGHT,
    BTN_DPAD_LEFT,
    BTN_DPAD_UP,
    
    // "center" buttons
    BTN_START,  // plus
    BTN_SELECT, // minus
    BTN_META,   // home, guide, etc

    BTN__COUNT
};

static constexpr size_t GAMEPAD_BUTTON_COUNT = static_cast<size_t>(gamepad_button::BTN__COUNT); 

enum class gamepad_axis
{
    GPA_NONE,
    GPA_ANALOG_LEFT_X,
    GPA_ANALOG_LEFT_Y,
    GPA_ANALOG_RIGHT_X,
    GPA_ANALOG_RIGHT_Y,
    GPA_TRIGGER_L,
    GPA_TRIGGER_R,

    GPA__COUNT
};

static constexpr size_t GAMEPAD_AXIS_COUNT = static_cast<size_t>(gamepad_axis::GPA__COUNT); 

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

struct gamepad_stored_state
{
    // this structure is used to collect gamepad events and store current state
    // this is to present to the engine a consistent state of the gamepad inbetween updates
    // ensures that very short presses are not missed, for example
    std::array<bool, GAMEPAD_BUTTON_COUNT> was_pressed {false};
    std::array<bool, GAMEPAD_BUTTON_COUNT> was_released {false};
    std::array<bool, GAMEPAD_BUTTON_COUNT> is_pressed {false};
    std::array<uint64_t, GAMEPAD_BUTTON_COUNT> when_pressed_ns {0};
    std::array<float, GAMEPAD_AXIS_COUNT> axis_value;
};

struct input_action
{
    entt::hashed_string id {};
    std::set<gamepad_button> gp_btns {};
    std::set<gamepad_axis> gp_axii {};
    std::set<uint32_t> kbd_scancodes {};

    bool directional {false};   // a directional action can map inputs to directions in mutiple axii
                                // the resulting event will combine all input states into a resulting vector
    std::unordered_map<gamepad_button, input_direction> dir_gp_btns;
    std::unordered_map<gamepad_axis, input_axis> dir_gp_axii;
    std::unordered_map<uint32_t, input_direction> dir_kbd_scancodes;
};

// used by input system to expose action state to engine
struct input_action_state
{
    bool was_pressed {false};
    bool was_released {false};
    bool is_pressed {false};
    std::array<float, 3> direction {.0f, .0f, .0f};
};

}