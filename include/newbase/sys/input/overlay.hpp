#pragma once

#include <newbase/ui/overlay.hpp>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_events.h>
#include <newbase/utility/glm.hpp>
#include <array>
#include <unordered_map>

namespace nb {

struct input_overlay : ui_overlay
{
    input_overlay();
    ~input_overlay();

    bool init();
    void shutdown();
    void event(const SDL_Event &event);
    void draw() const;

    bool enabled() const { return _enabled; }
    void set_enabled(bool enabled);
    void set_dpad_mode(bool dpad);
    void set_visible(bool visible) { _visible = visible; }

private:
    enum class control : unsigned char {
        LEFT_STICK, SOUTH, EAST, WEST, NORTH,
        DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT
    };

    struct layout
    {
        glm::vec2 stick_center;
        float stick_radius;
        std::array<glm::vec2, 4> button_centers;
        float button_radius;
    };

    layout current_layout() const;
    void begin_touch(SDL_FingerID finger, glm::vec2 position);
    void update_touch(SDL_FingerID finger, glm::vec2 position);
    void end_touch(SDL_FingerID finger);
    void update_stick(glm::vec2 position, const layout &layout);
    void update_dpad(glm::vec2 position, const layout &layout);
    void set_dpad_direction(glm::vec2 direction);
    void set_button(control control, bool pressed);
    void reset_controls();

    SDL_JoystickID _joystick_id {0};
    SDL_Joystick *_joystick {nullptr};
    std::unordered_map<SDL_FingerID, control> _touches;
    glm::vec2 _stick_position {0.0f, 0.0f};
    std::array<bool, 4> _buttons {false, false, false, false};
    std::array<bool, 4> _dpad {false, false, false, false};
    bool _enabled {false};
    bool _visible {false};
    bool _dpad_mode {false};
};

}
