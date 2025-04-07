#include <newbase/input/input.h>
#include <newbase/log.h>
#include <newbase/reflection/contexts.h>
#include <newbase/reflection/data.h>
#include <SDL3/SDL_gamepad.h>
#include <entt/entt.hpp>
#include <unordered_map>

using namespace nb;
using entt::operator""_hs;

struct gamepad_data
{
    uint32_t id{0};
    int player_index {-1};
    bool has_rumble {false};
    bool has_gyro {false};
    bool has_accel {false};
};

struct nb::input_p 
{
    std::unordered_map<int, SDL_Gamepad*> gamepads;
    std::unordered_map<int, gamepad_data> gps_data;
    std::unordered_map<entt::id_type, input_action> actions;
};

input::input()
{
    _d = new input_p();
}

input::~input()
{
    delete _d;
}

SDL_InitFlags input::sdl_subsystems(ryml::ConstNodeRef cfg)
{
    return SDL_INIT_GAMEPAD;
}

void input::bind(void *state) 
{
    log::warn("[input] unimplemented: bind");
}

bool input::init(ryml::ConstNodeRef cfg) 
{
    log::info("[input] init");

    if(cfg.invalid() || cfg.empty())
    {
        log::info("[input] no input config, setting up default action map");
        setup_default_actions();
    }
    else
    {
        log::error("[input] config parsing unimplemented! using default actions for now!");
        setup_default_actions();
    }

    return true;
}

bool input::step(step_phase) 
{
    // TODO process buffered inputs and emit action events
    return true;
}

bool input::event(SDL_Event *evt) 
{
    if(evt->type == SDL_EVENT_GAMEPAD_ADDED)
    {
        log::info("[input] event: gamepad added: %u", evt->gdevice.which);
        gamepad_add(evt->gdevice.which);
    }
    else if(evt->type == SDL_EVENT_GAMEPAD_REMOVED)
    {
        log::info("[input] event: gamepad removed: %u", evt->gdevice.which);
        gamepad_remove(evt->gdevice.which);
    }
    else if(evt->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
        // TODO buffer input
    }
    else if(evt->type == SDL_EVENT_GAMEPAD_BUTTON_UP)
    {
        // TODO buffer input
    }
    else if(evt->type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
    {
        // TODO buffer input
    }

    return true;
}

bool input::action_add(const input_action &action)
{
    _d->actions.emplace(action.id, input_action{action});
    return true;
}

void input::gamepad_add(uint32_t joy_id)
{
    // a new gamepad was added, register it

    SDL_Gamepad *gp = SDL_OpenGamepad(joy_id);
    if(gp)
    {
        auto p = SDL_GetGamepadProperties(gp);
        _d->gamepads[joy_id] = gp;
        _d->gps_data[joy_id] = gamepad_data {
            .id = joy_id,
            .has_rumble = SDL_GetBooleanProperty(p, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false),
            .has_gyro = SDL_GamepadHasSensor(gp, SDL_SENSOR_GYRO),
            .has_accel = SDL_GamepadHasSensor(gp, SDL_SENSOR_ACCEL)
        };
        log::info("[input] gamepad_add: registered: %d", joy_id);
    }
    else
        log::warn("[input] gamepad_add: cannot open: %d: %s", joy_id, SDL_GetError());
}

void input::gamepad_remove(uint32_t joy_id)
{
    auto it = _d->gamepads.find(joy_id);
    if(it == _d->gamepads.end())
    {
        log::warn("[input] gamepad_remove: not registered: %d");
        return;
    }
    SDL_CloseGamepad(it->second);
    _d->gps_data.erase(it->first);
    _d->gamepads.erase(it);
    log::info("[input] gamepad_remove: removed %d");
}

void input::setup_default_actions()
{
    action_add(input_action{
        .id = entt::hashed_string{"dir_primary"},
        .gp_btns = {gamepad_button::BTN_DPAD_UP, gamepad_button::BTN_DPAD_DOWN, gamepad_button::BTN_DPAD_LEFT, gamepad_button::BTN_DPAD_RIGHT},
        .gp_axii = {gamepad_axis::GPA_AXIS_LEFT_X, gamepad_axis::GPA_AXIS_LEFT_Y},
        .kbd_scancodes = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT},
        .directional = true,
        .dir_gp_btns = {
            {gamepad_button::BTN_DPAD_UP, input_direction::IDIR_UP},
            {gamepad_button::BTN_DPAD_DOWN, input_direction::IDIR_DOWN},
            {gamepad_button::BTN_DPAD_LEFT, input_direction::IDIR_LEFT},
            {gamepad_button::BTN_DPAD_RIGHT, input_direction::IDIR_RIGHT},
        },
        .dir_gp_axii = {
            {gamepad_axis::GPA_AXIS_LEFT_X, input_axis::IAXIS_X}, 
            {gamepad_axis::GPA_AXIS_LEFT_Y, input_axis::IAXIS_Y}
        },
        .dir_kbd_scancodes = {
            {SDL_SCANCODE_UP, input_direction::IDIR_UP},
            {SDL_SCANCODE_DOWN, input_direction::IDIR_DOWN},
            {SDL_SCANCODE_LEFT, input_direction::IDIR_LEFT},
            {SDL_SCANCODE_RIGHT, input_direction::IDIR_RIGHT},
        },
    });
    log::info("[input] default actions ready");
}


// RTTI metadata
extern "C" void _rtti_init_input()
{
    entt::meta_factory<nb::input>{}
        .type("input"_hs)
        .custom<rtti::system_info>(rtti::system_info{"input"})
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::input>>{rtti::ctx_systems()}
        .type("input_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::input>>()
        .conv<std::shared_ptr<nb::system>>();
}