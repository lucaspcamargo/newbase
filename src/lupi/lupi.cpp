#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/lupi/lupi.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/input/input.hpp>
#include <newbase/log.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/sprite.hpp>
#include "lupi_internal.hpp"
#include <entt/locator/locator.hpp>
#include <imgui.h>
#include <ryml_std.hpp>
#include <algorithm>

using namespace nb;

// ---------------------------------------------------------------------------
// lupi_self — every lua_CFunction binding fetches the owning lupi_p* through
// the Lua registry, stashed there once at init() time.
// ---------------------------------------------------------------------------

static const char* k_registry_key = "lupi_p";

lupi_p* nb::lupi_self(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, k_registry_key);
    auto* p = static_cast<lupi_p*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return p;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

namespace {

renderer_service* rs() { return entt::locator<renderer_service*>::has_value() ? entt::locator<renderer_service*>::value() : nullptr; }
ui_manager*      uim() { return entt::locator<ui_manager*>::has_value()       ? entt::locator<ui_manager*>::value()       : nullptr; }
input*           ins() { return entt::locator<input*>::has_value()           ? entt::locator<input*>::value()            : nullptr; }

// Player 0 keyboard mapping (see doc/system_lupi.md); players 1-2 and
// button ids 6-11 are reserved/unbound in this MVP. Always active,
// independent of whether a gamepad or the `input` system are around — see
// k_lupi_gp_actions below for the joypad side of player 0's bindings.
struct key_binding { int button_id; SDL_Scancode key1; SDL_Scancode key2; };
constexpr key_binding k_player0_keys[] = {
    {0, SDL_SCANCODE_LEFT,  SDL_SCANCODE_A}, // LEFT
    {1, SDL_SCANCODE_RIGHT, SDL_SCANCODE_D}, // RIGHT
    {2, SDL_SCANCODE_UP,    SDL_SCANCODE_W}, // UP
    {3, SDL_SCANCODE_DOWN,  SDL_SCANCODE_S}, // DOWN
    {4, SDL_SCANCODE_Z,     SDL_SCANCODE_Z}, // BTN_Z
    {5, SDL_SCANCODE_X,     SDL_SCANCODE_X}, // BTN_X
    {12, SDL_SCANCODE_F,    SDL_SCANCODE_F}, // BTN_F
    {13, SDL_SCANCODE_G,    SDL_SCANCODE_G}, // BTN_G
    {14, SDL_SCANCODE_Q,    SDL_SCANCODE_Q}, // BTN_Q
    {15, SDL_SCANCODE_E,    SDL_SCANCODE_E}, // BTN_E
};

// ---------------------------------------------------------------------------
// Player 0 gamepad mapping, via the `input` system's action API — only used
// when that system is actually registered (entt::locator<input*>), so a
// build without it falls back to keyboard-only, unchanged. These are lupi's
// own private actions (distinct ids from `input`'s "default actions" set),
// deliberately registered with NO keyboard scancodes of their own: keyboard
// input for lupi stays exclusively on k_player0_keys above, never routed
// through `input`, so the two paths can never double up or diverge.
//
// Button layout has no authoritative source (lupinho's own gamepad-constant
// table is internally inconsistent/buggy — BTN_Z gets assigned twice, and
// BTN_X isn't wired to a gamepad button at all), so this follows AGENTS.md's
// categorization instead: BTN_Z/BTN_X as the two primary face buttons,
// BTN_F/BTN_G as the other two, BTN_Q/BTN_E as the two shoulder bumpers.
struct gp_button_binding { int button_id; entt::hashed_string action_id; gamepad_button gp_btn; };
constexpr gp_button_binding k_lupi_gp_buttons[] = {
    {4,  entt::hashed_string{"lupi_btn_z"}, gamepad_button::BTN_SOUTH},
    {5,  entt::hashed_string{"lupi_btn_x"}, gamepad_button::BTN_WEST},
    {12, entt::hashed_string{"lupi_btn_f"}, gamepad_button::BTN_EAST},
    {13, entt::hashed_string{"lupi_btn_g"}, gamepad_button::BTN_NORTH},
    {14, entt::hashed_string{"lupi_btn_q"}, gamepad_button::BTN_BUMPER_L},
    {15, entt::hashed_string{"lupi_btn_e"}, gamepad_button::BTN_BUMPER_R},
};
constexpr entt::hashed_string k_lupi_gp_dir{"lupi_dir"};

// Registered once, lazily, the first time `input` is observed available —
// not from lupi::init(), since system init order between `lupi` and `input`
// isn't guaranteed.
void register_lupi_gamepad_actions(input* in)
{
    for (const auto& b : k_lupi_gp_buttons) {
        in->action_add(input_action{
            /*.id =*/ b.action_id,
            /*.gp_btns =*/ {b.gp_btn},
            /*.gp_axii =*/ {},
            /*.kbd_scancodes =*/ {},
            /*.directional =*/ false,
            /*.dir_gp_btns =*/ {},
            /*.dir_gp_axii =*/ {},
            /*.dir_kbd_scancodes =*/ {},
        });
    }
    in->action_add(input_action{
        /*.id =*/ k_lupi_gp_dir,
        /*.gp_btns =*/ {},
        /*.gp_axii =*/ {gamepad_axis::GPA_ANALOG_LEFT_X, gamepad_axis::GPA_ANALOG_LEFT_Y},
        /*.kbd_scancodes =*/ {},
        /*.directional =*/ true,
        /*.dir_gp_btns =*/ {
            {gamepad_button::BTN_DPAD_UP,    input_direction::IDIR_UP},
            {gamepad_button::BTN_DPAD_DOWN,  input_direction::IDIR_DOWN},
            {gamepad_button::BTN_DPAD_LEFT,  input_direction::IDIR_LEFT},
            {gamepad_button::BTN_DPAD_RIGHT, input_direction::IDIR_RIGHT},
        },
        /*.dir_gp_axii =*/ {
            {gamepad_axis::GPA_ANALOG_LEFT_X, input_axis::IAXIS_X},
            {gamepad_axis::GPA_ANALOG_LEFT_Y, input_axis::IAXIS_Y},
        },
        /*.dir_kbd_scancodes =*/ {},
    });
}

}

// ---------------------------------------------------------------------------
// nb::lupi system
// ---------------------------------------------------------------------------

lupi::lupi() : _d(new lupi_p) {}

lupi::~lupi()
{
    if (auto* m = uim()) m->unregister_tool_window("lupi framebuffer");
    if (_d->L) lua_close(_d->L);
    if (auto* r = rs(); r && _d->tex) r->destroy_texture(_d->tex);
    delete _d;
}

bool lupi::init(ryml::ConstNodeRef cfg)
{
    _d->rgba_scratch.resize(static_cast<size_t>(LUPI_SCREEN_W) * LUPI_SCREEN_H);

    // --- GPU texture + debug tool window (persistent for the system's lifetime) ---
    if (auto* r = rs()) {
        _d->tex = r->create_texture(LUPI_SCREEN_W, LUPI_SCREEN_H);
    }
    if (auto* m = uim()) {
        m->register_tool_window("lupi framebuffer", [this](bool* open) {
            if (!ImGui::Begin("Lupi Framebuffer", open)) { ImGui::End(); return; }
            if (_d->tex) {
                constexpr float W = (float)LUPI_SCREEN_W, H = (float)LUPI_SCREEN_H;
                ImVec2 avail = ImGui::GetContentRegionAvail();
                float scale = std::min(avail.x / W, avail.y / H);
                if (scale <= 0.f) scale = 1.f;
                ImGui::Image((ImTextureID)_d->tex, ImVec2(W * scale, H * scale));
            } else {
                ImGui::TextDisabled("(no renderer service)");
            }
            ImGui::End();
        });
    }
    engine::instance().debug_action_register("Lupi Framebuffer", [](){
        if (auto* m = uim())
            m->toggle_tool_window("lupi framebuffer");
    }, 10);

    // Optional convenience: auto-start a cart configured directly on the
    // system (e.g. `lupi: { cart: ... }`). Not required — carts are normally
    // started/stopped at runtime via the RTTI-exposed lupi_start()/lupi_stop()
    // Lua functions, e.g. from a scene's own script.
    if (cfg.has_child("cart")) {
        std::string cart_path;
        cfg["cart"] >> cart_path;
        if (!start(cart_path))
            log::warn("[lupi] configured cart '%s' failed to start at init", cart_path.c_str());
    }

    log::info("[lupi] ready (idle)");
    return true;
}

bool lupi::start(const std::string& cart_path)
{
    stop();

    auto cart_id = entt::hashed_string{cart_path.c_str()}.value();
    auto cart = rman().get<rlupi_cart>(cart_id);
    if (!cart || !cart->valid) {
        log::error("[lupi] failed to load cart '%s'", cart_path.c_str());
        return false;
    }
    _d->cart = cart;
    _d->cart_dir = cart->dir;
    _d->sprite_by_name.clear();

    _d->fb = lupi_framebuffer{};
    _d->pal = lupi_palette{};
    _d->gfx = lupi_gfx_state{};
    _d->frame_counter = 0;

    // Palette index 0 is the transparency sentinel (matches lupi-codec's own
    // Palette[1] = "0x0000" — see colors_manager.lua) — seed it explicitly so
    // it counts as "allocated" for lupi_scan_cart_assets' Palette synthesis.
    _d->pal.set(0, 0x0000);

    // --- lua_State: own, isolated sandbox — never shared with script_lua ---
    _d->L = luaL_newstate();
    if (!_d->L) {
        log::error("[lupi] luaL_newstate failed");
        _d->cart = nullptr;
        return false;
    }
    luaL_openlibs(_d->L);
    lua_pushlightuserdata(_d->L, _d);
    lua_setfield(_d->L, LUA_REGISTRYINDEX, k_registry_key);

    lupi_register_ui(_d->L);
    lupi_register_input(_d->L);
    lupi_register_stubs(_d->L);
    lupi_register_print(_d->L);
    lupi_register_require(_d->L, *_d);

    // Real carts ship no asset manifest at all — auto-discover every .png
    // under the cart directory (Sprites/Palette globals) and compile every
    // Tiled map JSON (satisfies "maps.<name>" requires) before running any
    // cart Lua, since game.lua's very first lines depend on both.
    lupi_scan_cart_assets(_d->L, *_d);
    lupi_compile_cart_maps(_d->L, *_d);

    // --- run the cart's game.lua top level (defines update(), etc) ---
    if (luaL_loadbuffer(_d->L, _d->cart->main_lua_src.c_str(), _d->cart->main_lua_src.size(),
                         _d->cart->chunkname.c_str()) != LUA_OK
        || lua_pcall(_d->L, 0, 0, 0) != LUA_OK) {
        log::error("[lupi] error loading cart '%s': %s", _d->cart->chunkname.c_str(), lua_tostring(_d->L, -1));
        lua_pop(_d->L, 1);
        lua_close(_d->L);
        _d->L = nullptr;
        _d->cart = nullptr;
        return false;
    }

    // --- show it in the default scene as an ordinary sprite ---
    if (!_d->screen_tex) {
        _d->screen_tex = std::make_shared<rtexture>(entt::hashed_string{"lupi_screen_texture"}.value());
        _d->screen_tex->tex = static_cast<SDL_Texture*>(_d->tex);
        _d->screen_tex->uploaded = true; // skip render_simple's lazy surf->tex upload — already have a live texture
        _d->screen_sprite = std::make_shared<rsprite>(entt::hashed_string{"lupi_screen_sprite"}.value());
        _d->screen_sprite->tex = _d->screen_tex;
    }
    auto& reg = engine::instance().default_scene().registry();
    _d->screen_entity = reg.create();
    auto& sp = reg.emplace<cspatial>(_d->screen_entity);
    sp.pos = {0.f, 0.f, 0.f};
    sp.apply();
    auto& spr = reg.emplace<csprite>(_d->screen_entity);
    spr.spr = _d->screen_sprite;

    _d->running = true;
    log::info("[lupi] started — cart '%s'", _d->cart->chunkname.c_str());
    return true;
}

void lupi::stop()
{
    if (_d->L) {
        lua_close(_d->L);
        _d->L = nullptr;
    }
    if (_d->screen_entity != entt::null) {
        auto& reg = engine::instance().default_scene().registry();
        if (reg.valid(_d->screen_entity))
            reg.destroy(_d->screen_entity);
        _d->screen_entity = entt::null;
    }
    if (_d->running)
        log::info("[lupi] stopped");
    _d->running = false;
    _d->cart = nullptr;
}

bool lupi::running() const
{
    return _d->running;
}

void lupi::on_scene_change()
{
    // The scene-owned screen_entity is about to be cleared along with the
    // rest of the scene; stop the cart too so it doesn't keep calling
    // update() with nowhere left to draw.
    stop();
}

bool lupi::step(step_phase phase)
{
    if (phase == GENERAL_UPDATE && _d->running) {
        Uint64 t0 = SDL_GetPerformanceCounter();

        _d->btn.pressure_last_frame = _d->btn.pressure_this_frame;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        for (const auto& kb : k_player0_keys) {
            bool down = keys[kb.key1] || keys[kb.key2];
            _d->btn.pressure_this_frame[0][kb.button_id] = down ? 255 : 0;
        }

        // Gamepad, via the `input` system's action API when it's around —
        // ORs into whatever the keyboard loop above already set, so either
        // source can hold a button down; never replaces/clears it.
        if (auto* in = ins()) {
            if (!_d->gamepad_actions_registered) {
                register_lupi_gamepad_actions(in);
                _d->gamepad_actions_registered = true;
            }
            for (const auto& b : k_lupi_gp_buttons) {
                if (in->action_is_pressed(b.action_id.value()))
                    _d->btn.pressure_this_frame[0][b.button_id] = 255;
            }
            glm::vec3 dir = in->action_direction(k_lupi_gp_dir.value());
            constexpr float kDirThreshold = 0.5f; // digital-from-analog trigger point
            if (dir.x < -kDirThreshold) _d->btn.pressure_this_frame[0][0] = 255; // LEFT
            if (dir.x >  kDirThreshold) _d->btn.pressure_this_frame[0][1] = 255; // RIGHT
            if (dir.y < -kDirThreshold) _d->btn.pressure_this_frame[0][2] = 255; // UP
            if (dir.y >  kDirThreshold) _d->btn.pressure_this_frame[0][3] = 255; // DOWN
        }

        lua_getglobal(_d->L, "update");
        lua_pushinteger(_d->L, (lua_Integer)_d->frame_counter);
        if (lua_pcall(_d->L, 1, 0, 0) != LUA_OK) {
            log::error("[lupi] update() error in '%s': %s", _d->cart->chunkname.c_str(), lua_tostring(_d->L, -1));
            lua_pop(_d->L, 1);
        }
        ++_d->frame_counter;

        Uint64 t1 = SDL_GetPerformanceCounter();
        double ms = double(t1 - t0) * 1000.0 / double(SDL_GetPerformanceFrequency());
        _d->last_step_ms = ms;
        double cpu_pct = std::min(100.0, ms / (1000.0 / 60.0) * 100.0);
        _d->cpu_ema = _d->cpu_ema * 0.9 + cpu_pct * 0.1;
        double fps = ms > 0.0 ? 1000.0 / ms : 60.0;
        _d->fps_ema = _d->fps_ema * 0.9 + std::min(fps, 60.0) * 0.1;
    }

    if (phase == RENDER) {
        // Palette index 0 always renders fully transparent, regardless of
        // whatever color ui.palset(0, ...) has set it to — confirmed against
        // the real engine's own get_palette_color() (lupinho/src/ui.c),
        // which hardcodes this special case rather than treating it as an
        // ordinary paletted color, and is independent of alpha in the raw
        // BGR555 encoding itself (there is none).
        for (size_t i = 0; i < _d->fb.pixels.size(); ++i) {
            uint8_t idx = _d->fb.pixels[i];
            _d->rgba_scratch[i] = idx == 0 ? 0u : lupi_palette::bgr555_to_rgba8888(_d->pal.bgr555[idx]);
        }
        if (auto* r = rs(); r && _d->tex)
            r->update_texture(_d->tex, _d->rgba_scratch.data(), LUPI_SCREEN_W * 4);
    }

    return true;
}

bool lupi::event(SDL_Event* ev)
{
    if (!_d->text_input_started) {
        if (SDL_Window* win = SDL_GetKeyboardFocus()) {
            SDL_StartTextInput(win);
            _d->text_input_started = true;
        }
    }

    switch (ev->type) {
        case SDL_EVENT_MOUSE_MOTION:
            _d->mouse.x = ev->motion.x;
            _d->mouse.y = ev->motion.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            _d->mouse.buttons |= (1u << ev->button.button);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            _d->mouse.buttons &= ~(1u << ev->button.button);
            break;
        case SDL_EVENT_TEXT_INPUT:
            _d->text.push(ev->text.text);
            break;
        default:
            break;
    }
    return true;
}

// ---------------------------------------------------------------------------
// RTTI
// ---------------------------------------------------------------------------
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/contexts.hpp>
#include <entt/meta/factory.hpp>

extern "C" void _rtti_init_lupi()
{
    using namespace entt::literals;

    entt::meta_factory<nb::lupi>{}
        .type("lupi"_hs)
        .custom<nb::rtti::type_info>(nb::rtti::type_info{"lupi", nb::rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>()
        .func<&nb::lupi::start>("start"_hs)
        .custom<nb::rtti::func_info>(nb::rtti::func_info{"start"})
        .func<&nb::lupi::stop>("stop"_hs)
        .custom<nb::rtti::func_info>(nb::rtti::func_info{"stop"})
        .func<&nb::lupi::running>("running"_hs)
        .custom<nb::rtti::func_info>(nb::rtti::func_info{"running"});

    entt::meta_factory<std::shared_ptr<nb::lupi>>{nb::rtti::ctx_systems()}
        .type("lupi_shared"_hs)
        .ctor<&nb::rtti::shared_ptr_builder<nb::lupi>>()
        .conv<std::shared_ptr<nb::system>>();

    entt::meta_factory<nb::rlupi_cart>{}
        .type("rlupi_cart"_hs)
        .custom<nb::rtti::type_info>(nb::rtti::type_info{
            .identifier = "lupi_cart",
            .type_class = nb::rtti::TYPE_CLASS_RESOURCE,
            .data = {.resource = {.editor_icon = nullptr, .extensions = "yaml"}}, // real carts: lupi.yaml
            .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                return rloader_lupi_cart{}(id);
            }
        });
}
