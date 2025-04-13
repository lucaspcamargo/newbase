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
static void _apply_dir_axis(std::array<float, 3> &arr, input_axis axis, float value);
static void _apply_dir(std::array<float, 3> &arr, input_direction dir);

struct gamepad_data
{
    uint32_t id{0};
    int player_index {-1};
    bool has_rumble {false};
    bool has_gyro {false};
    bool has_accel {false};
    bool ninty_layout {false};
    gamepad_stored_state state;
};

struct nb::input_p 
{
    std::unordered_map<int, SDL_Gamepad*> gamepads;
    std::unordered_map<int, gamepad_data> gp_data;
    
    std::unordered_map<entt::id_type, input_action> actions;
    std::unordered_map<entt::id_type, input_action_state> action_states;

    // find actions from specific input
    // not used internally because of the one-shot processing
    std::unordered_map<SDL_Scancode, std::set<entt::id_type>> actions_for_kbd;
    std::unordered_map<gamepad_button, std::set<entt::id_type>> actions_for_gp_btn;
    std::unordered_map<gamepad_axis, std::set<entt::id_type>> actions_for_gp_axis;

    // buffered keyboard state
    std::set<SDL_Scancode> kbd_is_pressed;
    std::set<SDL_Scancode> kbd_was_pressed;
    std::set<SDL_Scancode> kbd_was_released;
    std::unordered_map<SDL_Scancode, uint64_t> kbd_when_pressed_ns;
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

    SDL_SetGamepadEventsEnabled(true);
    log::info("[input] enabled gamepads");

    return true;
}

bool input::step(step_phase phase) 
{
    if(phase == step_phase::PRE_UPDATE)
    {
        // process buffered inputs and update action states
        for(auto &action_p: _d->actions)
        {
            const auto &action = action_p.second;
            auto &action_state = _d->action_states[action_p.first];
            std::array<float, 3> &dir_result  = action_state.direction;
            
            // reset action state
            action_state = {};
            action_state.direction.fill(0.f);
            
            // first, gamepads
            for(auto &gp_p: _d->gamepads)
            {
                const auto gp = gp_p.second;
                const auto &gp_data = _d->gp_data[gp_p.first];
                const auto &gp_state = gp_data.state;
                
                for(gamepad_button gpbtn: action.gp_btns)
                {
                    if(gp_state.is_pressed[static_cast<size_t>(gpbtn)])
                        action_state.is_pressed = true;
                    if(gp_state.was_pressed[static_cast<size_t>(gpbtn)])
                        action_state.was_pressed = true;
                    if(gp_state.was_released[static_cast<size_t>(gpbtn)])
                        action_state.was_released = true;
                }
                
                if(action.directional)
                {
                    for(auto [btn, dir]: action.dir_gp_btns)
                    {
                        if(gp_state.is_pressed[static_cast<size_t>(btn)])
                            _apply_dir(dir_result, dir);
                    }
                    for(auto [axis, iaxis]: action.dir_gp_axii)
                    {
                        _apply_dir_axis(dir_result, iaxis, gp_state.axis_value[static_cast<size_t>(axis)]);
                    }
                }
                
            }

            // then, keyboard
            for (uint32_t kbd_u32 : action.kbd_scancodes)
            {
                SDL_Scancode kbd_sc = static_cast<SDL_Scancode>(kbd_u32);
                if (_d->kbd_is_pressed.find(kbd_sc) != _d->kbd_is_pressed.end())
                    action_state.is_pressed = true;
                if (_d->kbd_was_pressed.find(kbd_sc) != _d->kbd_was_pressed.end())
                    action_state.was_pressed = true;
                if (_d->kbd_was_released.find(kbd_sc) != _d->kbd_was_released.end())
                    action_state.was_released = true;
            }

            // directional general
            if(action.directional)
            {
                // directional global inputs
                for(auto [sc_u32, dir]: action.dir_kbd_scancodes)
                {
                    const SDL_Scancode sc = static_cast<SDL_Scancode>(sc_u32);
                    if(_d->kbd_is_pressed.find(sc) != _d->kbd_is_pressed.end())
                        _apply_dir(dir_result, dir);
                }

                float len = sqrtf(dir_result[0]*dir_result[0] +
                            dir_result[1]*dir_result[1] +
                            dir_result[2]*dir_result[2]);
                if(len > 1.0f)
                {
                    dir_result[0] /= len;
                    dir_result[1] /= len;
                    dir_result[2] /= len;
                }
            }
        }
        
        // cleanup stored states for further events
        _d->kbd_was_pressed.clear();
        _d->kbd_was_released.clear();
        for(auto &gp:_d->gp_data)
        {
            gp.second.state.was_pressed.fill(false);
            gp.second.state.was_released.fill(false);
        }
    }
    
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
                log::verb("[input] gp pressed: %d", (int) btn);
                auto &state = _d->gp_data[joy_id].state;
                state.is_pressed[static_cast<size_t>(btn)] = true;
                state.was_pressed[static_cast<size_t>(btn)] = true;
                if(!state.when_pressed_ns[static_cast<size_t>(btn)])
                    state.when_pressed_ns[static_cast<size_t>(btn)] = evt->gbutton.timestamp;
            }
        }
        else
            log::warn("[input] evt without gp: %d", joy_id);
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
                log::verb("[input] gp released: %d", static_cast<int>(btn));
                auto &state = _d->gp_data[joy_id].state;
                state.is_pressed[static_cast<size_t>(btn)] = false;
                state.was_released[static_cast<size_t>(btn)] = true;
            }
        }
        else
            log::warn("[input] evt without gp: %d", joy_id);
    }
    else if(evt->type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
    {
        auto joy_id = evt->gaxis.which;
        auto gpit = _d->gamepads.find(joy_id);
        if(gpit != _d->gamepads.end())
        {
            const SDL_GamepadAxis sdl_axis = static_cast<SDL_GamepadAxis>(evt->gaxis.axis);
            gamepad_axis axis = _conv_gp_axis(sdl_axis);
            if(axis != gamepad_axis::GPA_NONE)
            {
                float our_value = evt->gaxis.value?
                                    (evt->gaxis.value>0
                                        ? evt->gaxis.value/32767.f
                                        : evt->gaxis.value/32768.f)
                                    :.0f;
                log::verb("[input] gp axis: %d %f", static_cast<int>(axis), our_value);
                auto &state = _d->gp_data[joy_id].state;
                state.axis_value[static_cast<size_t>(axis)] = our_value;
            }
        }
        else
            log::warn("[input] evt without gp: %d", joy_id);
    }
    else if(evt->type == SDL_EVENT_KEY_DOWN)
    {
        const SDL_Scancode sc = evt->key.scancode;
        log::verb("[input] key down: %d", static_cast<int>(sc));
        _d->kbd_is_pressed.insert(sc);
        _d->kbd_was_pressed.insert(sc);
        auto it = _d->kbd_when_pressed_ns.find(sc);
        if(it == _d->kbd_when_pressed_ns.end())
            _d->kbd_when_pressed_ns.emplace(sc, evt->key.timestamp);
    }
    else if(evt->type == SDL_EVENT_KEY_UP)
    {
        const SDL_Scancode sc = evt->key.scancode;
        log::verb("[input] key up: %d", static_cast<int>(sc));
        _d->kbd_is_pressed.erase(sc);
        _d->kbd_was_released.insert(sc);
    }

    return true;
}

bool input::action_add(const input_action &action)
{
    log::info("[input] action_add: %s (%x)", action.id.operator const char *(), action.id.value());
    _d->actions.emplace(action.id, input_action{action});
    for(auto kbd_u32: action.kbd_scancodes)
    {
        const auto kbd = static_cast<SDL_Scancode>(kbd_u32);
        _d->actions_for_kbd[kbd].insert(action.id);
    }
    for(auto btn: action.gp_btns)
    {
        _d->actions_for_gp_btn[btn].insert(action.id);
    }
    for(auto axis: action.gp_axii)
    {
        _d->actions_for_gp_axis[axis].insert(action.id);
    }

    return true;
}


void input::action_remove(entt::id_type action_id)
{
    auto it = _d->actions.find(action_id);
    if(it == _d->actions.end())
        return;

    const auto &action = it->second;

    for(const auto kbd_u32: action.kbd_scancodes)
    {
        const auto kbd = static_cast<SDL_Scancode>(kbd_u32);
        _d->actions_for_kbd[kbd].erase(action.id);
        if(_d->actions_for_kbd[kbd].empty())
            _d->actions_for_kbd.erase(kbd);
    }
    for(auto btn: action.gp_btns)
    {
        _d->actions_for_gp_btn[btn].erase(action.id);
        if(_d->actions_for_gp_btn[btn].empty())
            _d->actions_for_gp_btn.erase(btn);
    }
    for(auto axis: action.gp_axii)
    {
        _d->actions_for_gp_axis[axis].erase(action.id);
        if(_d->actions_for_gp_axis[axis].empty())
            _d->actions_for_gp_axis.erase(axis);
    }
    _d->actions.erase(it);
}

bool input::action_is_pressed(entt::id_type action_id)
{
    auto it = _d->action_states.find(action_id);
    if(it != _d->action_states.end())
    {
        if(it->second.is_pressed)
            return true;
    }
    return false;
}

bool input::action_was_pressed(entt::id_type action_id)
{
    auto it = _d->action_states.find(action_id);
    if(it != _d->action_states.end())
    {
        if(it->second.was_pressed)
            return true;
    }
    return false;
}

bool input::action_was_released(entt::id_type action_id)
{
    auto it = _d->action_states.find(action_id);
    if(it != _d->action_states.end())
    {
        if(it->second.was_released)
            return true;
    }
    return false;
}


std::array<float, 3> input::action_direction(entt::id_type action_id)
{

    auto it = _d->action_states.find(action_id);
    if(it != _d->action_states.end())
    {
        return it->second.direction;
    }
    return {.0f, .0f, .0f};
}

void input::gamepad_add(uint32_t joy_id)
{
    // a new gamepad was added, register it

    SDL_Gamepad *gp = SDL_OpenGamepad(joy_id);
    if(gp)
    {
        auto p = SDL_GetGamepadProperties(gp);
        _d->gamepads[joy_id] = gp;
        _d->gp_data[joy_id] = gamepad_data {
            .id = joy_id,
            .has_rumble = SDL_GetBooleanProperty(p, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false),
            .has_gyro = SDL_GamepadHasSensor(gp, SDL_SENSOR_GYRO),
            .has_accel = SDL_GamepadHasSensor(gp, SDL_SENSOR_ACCEL),
            .ninty_layout = SDL_GetGamepadButtonLabel(gp, SDL_GAMEPAD_BUTTON_SOUTH) == SDL_GAMEPAD_BUTTON_LABEL_B
        };
        auto name = SDL_GetGamepadName(gp);
        log::info("[input] gamepad_add: registered: '%s' (%d)%s%s%s %d", 
            name? name : "",
            joy_id, 
            _d->gp_data[joy_id].has_rumble? " rumble":"",
            _d->gp_data[joy_id].has_gyro? " gyro":"",
            _d->gp_data[joy_id].has_accel? " accel":"",
            _d->gp_data[joy_id].ninty_layout? " ninty":"");
            SDL_SetGamepadPlayerIndex(gp, 1);
    }
    else
        log::warn("[input] gamepad_add: cannot open: %d: %s", joy_id, SDL_GetError());
}

void input::gamepad_remove(uint32_t joy_id)
{
    auto it = _d->gamepads.find(joy_id);
    if(it == _d->gamepads.end())
    {
        log::warn("[input] gamepad_remove: not registered: %d", joy_id);
        return;
    }
    SDL_CloseGamepad(it->second);
    _d->gp_data.erase(it->first);
    _d->gamepads.erase(it);
    log::info("[input] gamepad_remove: removed %d", joy_id);
}

void input::setup_default_actions()
{
    action_add(input_action{
        .id = entt::hashed_string{"dir"},
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

static void _apply_dir_axis(std::array<float, 3> &arr, input_axis axis, float val)
{
    switch(axis)
    {
        case input_axis::IAXIS_X:
            arr[0] += val;
            return;
        case input_axis::IAXIS_Y:
            arr[1] += val;
            return;
        case input_axis::IAXIS_Z:
            arr[2] += val;
            return;
    }
}


static void _apply_dir(std::array<float, 3> &arr, input_direction dir)
{
    switch(dir)
    {
        case input_direction::IDIR_RIGHT:
            arr[0] += 1.0f;
            return;
        case input_direction::IDIR_LEFT:
            arr[0] -= 1.0f;
            return;
        case input_direction::IDIR_DOWN:
            arr[1] += 1.0f;
            return;
        case input_direction::IDIR_UP:
            arr[1] -= 1.0f;
            return;
        case input_direction::IDIR_FORWARD:
            arr[2] += 1.0f;
            return;
        case input_direction::IDIR_BACKWARD:
            arr[2] -= 1.0f;
            return;
    }
}