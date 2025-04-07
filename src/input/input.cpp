#include <newbase/input/input.h>
#include <newbase/log.h>
#include <newbase/reflection/contexts.h>
#include <newbase/reflection/data.h>
#include <SDL3/SDL_gamepad.h>
#include <entt/entt.hpp>
#include <unordered_map>

using namespace nb;
using entt::operator""_hs;

static gamepad_button _conv_gp_button(SDL_GamepadButton btn);
static gamepad_axis _conv_gp_axis(SDL_GamepadAxis axis);

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
        auto joy_id = evt->gbutton.which;
        auto gpit = _d->gamepads.find(joy_id);
        if(gpit != _d->gamepads.end())
        {
            const SDL_GamepadButton sdl_btn = static_cast<SDL_GamepadButton>(evt->gbutton.button);
            gamepad_button btn = _conv_gp_button(sdl_btn);
            if(btn != gamepad_button::BTN_NONE)
            {
                log::info("[input] gp pressed: %d", (int) btn);
            }
        }
    }
    else if(evt->type == SDL_EVENT_GAMEPAD_BUTTON_UP)
    {
        auto joy_id = evt->gbutton.which;
        auto gpit = _d->gamepads.find(joy_id);
        if(gpit != _d->gamepads.end())
        {
            const SDL_GamepadButton sdl_btn = static_cast<SDL_GamepadButton>(evt->gbutton.button);
            gamepad_button btn = _conv_gp_button(sdl_btn);
            if(btn != gamepad_button::BTN_NONE)
            {
                log::info("[input] gp released: %d", (int) btn);
            }
        }
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


void input::action_remove(entt::id_type action_id)
{
    _d->actions.erase(action_id);
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
        log::info("[input] gamepad_add: registered: %d%s%s%s", 
            joy_id, 
            _d->gps_data[joy_id].has_rumble? " rumble":"",
            _d->gps_data[joy_id].has_gyro? " gyro":"",
            _d->gps_data[joy_id].has_accel? " accel":"");
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
        .gp_axii = {gamepad_axis::GPA_ANALOG_LEFT_X, gamepad_axis::GPA_ANALOG_LEFT_Y},
        .kbd_scancodes = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT},
        .directional = true,
        .dir_gp_btns = {
            {gamepad_button::BTN_DPAD_UP, input_direction::IDIR_UP},
            {gamepad_button::BTN_DPAD_DOWN, input_direction::IDIR_DOWN},
            {gamepad_button::BTN_DPAD_LEFT, input_direction::IDIR_LEFT},
            {gamepad_button::BTN_DPAD_RIGHT, input_direction::IDIR_RIGHT},
        },
        .dir_gp_axii = {
            {gamepad_axis::GPA_ANALOG_LEFT_X, input_axis::IAXIS_X}, 
            {gamepad_axis::GPA_ANALOG_LEFT_Y, input_axis::IAXIS_Y}
        },
        .dir_kbd_scancodes = {
            {SDL_SCANCODE_UP, input_direction::IDIR_UP},
            {SDL_SCANCODE_DOWN, input_direction::IDIR_DOWN},
            {SDL_SCANCODE_LEFT, input_direction::IDIR_LEFT},
            {SDL_SCANCODE_RIGHT, input_direction::IDIR_RIGHT},
        },
    });
    action_add(input_action{
        .id = entt::hashed_string{"dir_secondary"},
        .gp_btns = {},
        .gp_axii = {gamepad_axis::GPA_ANALOG_RIGHT_X, gamepad_axis::GPA_ANALOG_RIGHT_Y},
        .kbd_scancodes = {SDL_SCANCODE_T, SDL_SCANCODE_G, SDL_SCANCODE_F, SDL_SCANCODE_H},
        .directional = true,
        .dir_gp_btns = {},
        .dir_gp_axii = {
            {gamepad_axis::GPA_ANALOG_RIGHT_X, input_axis::IAXIS_X}, 
            {gamepad_axis::GPA_ANALOG_RIGHT_Y, input_axis::IAXIS_Y}
        },
        .dir_kbd_scancodes = {
            {SDL_SCANCODE_T, input_direction::IDIR_UP},
            {SDL_SCANCODE_G, input_direction::IDIR_DOWN},
            {SDL_SCANCODE_F, input_direction::IDIR_LEFT},
            {SDL_SCANCODE_H, input_direction::IDIR_RIGHT},
        },
    });
    action_add(input_action{
        .id = entt::hashed_string{"btn_south"},
        .gp_btns = {gamepad_button::BTN_SOUTH},
        .gp_axii = {},
        .kbd_scancodes = {SDL_SCANCODE_Z},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"btn_west"},
        .gp_btns = {gamepad_button::BTN_WEST},
        .gp_axii = {},
        .kbd_scancodes = {SDL_SCANCODE_A},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"btn_east"},
        .gp_btns = {gamepad_button::BTN_EAST},
        .gp_axii = {},
        .kbd_scancodes = {SDL_SCANCODE_X},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"btn_north"},
        .gp_btns = {gamepad_button::BTN_NORTH},
        .gp_axii = {},
        .kbd_scancodes = {SDL_SCANCODE_S},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"bumper_l"},
        .gp_btns = {gamepad_button::BTN_BUMPER_L},
        .gp_axii = {},
        .kbd_scancodes = {SDL_SCANCODE_Q},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"bumper_r"},
        .gp_btns = {gamepad_button::BTN_BUMPER_R},
        .gp_axii = {},
        .kbd_scancodes = {SDL_SCANCODE_W},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"trigger_l"},
        .gp_btns = {gamepad_button::BTN_TRIGGER_L},
        .gp_axii = {gamepad_axis::GPA_TRIGGER_L},
        .kbd_scancodes = {SDL_SCANCODE_1},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"trigger_r"},
        .gp_btns = {gamepad_button::BTN_TRIGGER_R},
        .gp_axii = {gamepad_axis::GPA_TRIGGER_R},
        .kbd_scancodes = {SDL_SCANCODE_2},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"start"},
        .gp_btns = {gamepad_button::BTN_START},
        .gp_axii = {},
        .kbd_scancodes = {SDL_SCANCODE_RETURN},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"select"},
        .gp_btns = {gamepad_button::BTN_SELECT},
        .gp_axii = {},
        .kbd_scancodes = {SDL_SCANCODE_RSHIFT, SDL_SCANCODE_TAB},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"l_stick"},
        .gp_btns = {gamepad_button::BTN_LS},
        .gp_axii = {},
        .kbd_scancodes = {SDL_SCANCODE_R},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
    });
    action_add(input_action{
        .id = entt::hashed_string{"r_stick"},
        .gp_btns = {gamepad_button::BTN_RS},
        .gp_axii = {},
        .kbd_scancodes = {SDL_SCANCODE_Y},
        .directional = false,
        .dir_gp_btns = {},
        .dir_gp_axii = {},
        .dir_kbd_scancodes = {},
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

// Conversion functions

static gamepad_button _conv_gp_button(SDL_GamepadButton btn)
{
    switch(btn)
    {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return gamepad_button::BTN_DPAD_UP;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return gamepad_button::BTN_DPAD_DOWN;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return gamepad_button::BTN_DPAD_LEFT;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return gamepad_button::BTN_DPAD_RIGHT;
        
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return gamepad_button::BTN_SOUTH;
        case SDL_GAMEPAD_BUTTON_NORTH:
            return gamepad_button::BTN_NORTH;
        case SDL_GAMEPAD_BUTTON_WEST:
            return gamepad_button::BTN_WEST;
        case SDL_GAMEPAD_BUTTON_EAST:
            return gamepad_button::BTN_EAST;
        
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return gamepad_button::BTN_BUMPER_L;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return gamepad_button::BTN_BUMPER_R;

        case SDL_GAMEPAD_BUTTON_START:
            return gamepad_button::BTN_START;
        case SDL_GAMEPAD_BUTTON_BACK:
            return gamepad_button::BTN_SELECT;
        case SDL_GAMEPAD_BUTTON_GUIDE:
            return gamepad_button::BTN_META;

        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return gamepad_button::BTN_LS;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return gamepad_button::BTN_RS;

        default:
            return gamepad_button::BTN_NONE;
    }

    return gamepad_button::BTN_NONE;
}

static gamepad_axis _conv_gp_axis(SDL_GamepadAxis axis)
{
    switch(axis)
    {
        case SDL_GAMEPAD_AXIS_LEFTX:
            return gamepad_axis::GPA_ANALOG_LEFT_X;
        case SDL_GAMEPAD_AXIS_RIGHTX:
            return gamepad_axis::GPA_ANALOG_RIGHT_X;
        case SDL_GAMEPAD_AXIS_LEFTY:
            return gamepad_axis::GPA_ANALOG_LEFT_Y;
        case SDL_GAMEPAD_AXIS_RIGHTY:
            return gamepad_axis::GPA_ANALOG_RIGHT_Y;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            return gamepad_axis::GPA_TRIGGER_L;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            return gamepad_axis::GPA_TRIGGER_R;
        default:
            return gamepad_axis::GPA_NONE;
    }
    return gamepad_axis::GPA_NONE;
}