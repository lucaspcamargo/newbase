#pragma once

#include <newbase/system.hpp>
#include <newbase/input/types.hpp>

struct SDL_Gamepad;

namespace nb {

struct input_p;

/**
 * The input system takes care of all game-related input.
 * It is responsible for:
 * - collecting SDL input events
 * - buffering inputs inbetween frame processing
 * - mapping input events to game actions
 * - presenting a consistent view of input methods and actions every frame
 *      - by parsing the buffered input before simulation updates
 *      - and then presenting all interested parties the same input state via its API
 */
class input : public system 
{
public:
    input();
    ~input();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override;
    entt::id_type metatype_id() override { return entt::hashed_string{"input"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

    bool action_add(const input_action &action);
    void action_remove(entt::id_type action_id);

    bool action_is_pressed(entt::id_type action_id);
    bool action_was_pressed(entt::id_type action_id);
    bool action_was_released(entt::id_type action_id);
    glm::vec3 action_direction(entt::id_type action_id);

    // unified mouse/touch pointer, in window logical pixels (origin top-left).
    // touch is tracked via the first active finger; scripts wanting world-space
    // coordinates should convert via the renderer_service's 2D extents.
    glm::vec2 pointer_position() const;
    bool pointer_is_pressed() const;
    bool pointer_was_pressed() const;
    bool pointer_was_released() const;

    bool overlay_enabled() const;
    void set_overlay_enabled(bool enabled);
    bool overlay_force() const;
    void set_overlay_force(bool force);
    bool overlay_dpad() const;
    void set_overlay_dpad(bool dpad);

    // we need api to identify different input devices
    // so that it can be assigned to different players
    // and also action state per player, which changes the above
    // currently, we assume single-player

    void rumble(float secs, float strength) {}

private:
    void gamepad_add(uint32_t joy_id);
    void gamepad_remove(uint32_t joy_id);
    void setup_default_actions();

    input_p *_d;
};

}