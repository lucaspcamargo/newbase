#pragma once

#include <newbase/system.h>
#include <newbase/input/types.h>

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
    bool can_bind() override { return true; }
    void bind(void *state) override;

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

    bool action_add(const input_action &action);
    void action_remove(entt::id_type action_id);

private:
    void gamepad_add(uint32_t joy_id);
    void gamepad_remove(uint32_t joy_id);
    void setup_default_actions();

    input_p *_d;
};

}