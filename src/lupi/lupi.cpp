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
#include <cstdio>

using namespace nb;

// ---------------------------------------------------------------------------
// lupi_self — every lua_CFunction binding fetches the owning lupi_p* through
// the Lua registry, stashed there once at init() time.
// ---------------------------------------------------------------------------

static const char *k_registry_key = "lupi_p";

lupi_p *nb::lupi_self(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, k_registry_key);
    auto *p = static_cast<lupi_p *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return p;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

namespace
{

    renderer_service *rs() { return entt::locator<renderer_service *>::has_value() ? entt::locator<renderer_service *>::value() : nullptr; }
    ui_manager *uim() { return entt::locator<ui_manager *>::has_value() ? entt::locator<ui_manager *>::value() : nullptr; }

    input *ins()
    {
        static const auto id = entt::hashed_string{"input"}.value();
        auto sys = engine::instance().system_from_id(id);
        return sys ? static_cast<input *>(sys.get()) : nullptr;
    }

    // ---------------------------------------------------------------------------
    // Player 0 mapping onto the `input` system's own default action map (see
    // input::setup_default_actions() in src/input/input.cpp) — used whenever
    // that system is registered and has those actions set up; k_player0_keys
    // above is only a fallback for when it isn't. lupi doesn't register any
    // actions of its own — the default action map already covers gamepad *and*
    // keyboard, so it doubles as the "virtual SNES controller" the real Lupi
    // console's own input maps onto (see doc/system_lupi.md).
    //
    // Button layout has no authoritative source (lupinho's own gamepad-constant
    // table is internally inconsistent/buggy — BTN_Z gets assigned twice, and
    // BTN_X isn't wired to a gamepad button at all), so this follows AGENTS.md's
    // categorization onto a SNES-style face-button layout (south=B, east=A,
    // west=Y, north=X): BTN_Z/BTN_X as the two primary action buttons (B/Y),
    // BTN_F/BTN_G as the other two (A/X), BTN_Q/BTN_E as the two shoulder bumpers.
    struct action_button_binding
    {
        int button_id;
        entt::hashed_string action_id;
    };
    constexpr action_button_binding k_lupi_gp_buttons[] = {
        {4, entt::hashed_string{"btn_south"}},  // BTN_Z -> SNES B
        {5, entt::hashed_string{"btn_west"}},   // BTN_X -> SNES Y
        {12, entt::hashed_string{"btn_east"}},  // BTN_F -> SNES A
        {13, entt::hashed_string{"btn_north"}}, // BTN_G -> SNES X
        {14, entt::hashed_string{"bumper_l"}},  // BTN_Q -> SNES L
        {15, entt::hashed_string{"bumper_r"}},  // BTN_E -> SNES R
    };
    constexpr entt::hashed_string k_lupi_gp_dir{"dir"};

}

// ---------------------------------------------------------------------------
// nb::lupi system
// ---------------------------------------------------------------------------

lupi::lupi() : _d(new lupi_p) {}

lupi::~lupi()
{
    if (auto *m = uim())
        m->unregister_tool_window("lupi framebuffer");
    if (_d->L)
        lua_close(_d->L);
    if (auto *r = rs(); r && _d->tex)
        r->destroy_texture(_d->tex);
    delete _d;
}

bool lupi::init(ryml::ConstNodeRef cfg)
{
    _d->rgba_scratch.resize(static_cast<size_t>(LUPI_SCREEN_W) * LUPI_SCREEN_H);

    if(auto *input_system = ins())
        input_system->set_overlay_dpad(true);

    // --- GPU texture + debug tool window (persistent for the system's lifetime) ---
    if (auto *r = rs())
    {
        _d->tex = r->create_texture(LUPI_SCREEN_W, LUPI_SCREEN_H);
    }
    if (auto *m = uim())
    {
        m->register_tool_window("lupi_debug", [this](bool *open)
                                {
            if (!ImGui::Begin("Lupi Debug", open)) { ImGui::End(); return; }
            ImGui::Checkbox("Fixed 60 FPS simulation", &_d->fixed_rate_enabled);
            if(_d->fixed_rate_enabled)
                                    ImGui::InputDouble("60 FPS snap tolerance (s)", 
                                        &_d->fixed_rate_snap_tolerance, 0.0001, 0.001, "%.4f");
            if(ImGui::TreeNodeEx("Framebuffer", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (_d->tex) {
                    constexpr float W = (float)LUPI_SCREEN_W, H = (float)LUPI_SCREEN_H;
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    float scale = std::min(avail.x / W, avail.y / H);
                    if (scale <= 0.f) scale = 1.f;
                    ImGui::Image((ImTextureID)_d->tex, ImVec2(W * scale, H * scale));
                } else {
                    ImGui::TextDisabled("(no renderer service)");
                }
                ImGui::TreePop();
            }
            if(ImGui::TreeNodeEx("Palette", ImGuiTreeNodeFlags_DefaultOpen))
            {
                float curr_text_size = ImGui::GetStyle().FontSizeBase;
                ImVec2 clr_size {curr_text_size, curr_text_size};
                for(int i = 0; i < _d->pal.allocated.size(); i++)
                {
                    if(i % 32)
                        ImGui::SameLine();
                    char clr_btn_id[32];
                    clr_btn_id[31] = '\0';
                    snprintf(clr_btn_id, 31, "lupi_pal_%d", i);
                    ImVec4 color = ImGui::ColorConvertU32ToFloat4(lupi_palette::bgr555_to_rgba8888(_d->pal.bgr555[i]));
                    if(!_d->pal.allocated[i])
                        color.w = 0.5f;
                    ImGui::ColorButton(clr_btn_id, color, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Uint8, clr_size);

                    // Mark palette entries that have not been allocated yet.
                    if (!_d->pal.allocated[i]) {
                        ImDrawList *draw_list = ImGui::GetWindowDrawList();
                        ImVec2 min = ImGui::GetItemRectMin();
                        ImVec2 max = ImGui::GetItemRectMax();
                        constexpr ImU32 unused_color = IM_COL32(220, 40, 40, 255);
                        draw_list->AddLine(min, max, unused_color, 1.5f);
                        draw_list->AddLine(ImVec2(max.x, min.y), ImVec2(min.x, max.y), unused_color, 1.5f);
                    }
                }
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Assets"))
            {
                if (ImGui::BeginTable("lupi_assets", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                     ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                                      ImVec2(0.0f, 240.0f)))
                {
                    ImGui::TableSetupColumn("Type");
                    ImGui::TableSetupColumn("Lupi identifier");
                    ImGui::TableSetupColumn("Dimensions");
                    ImGui::TableSetupColumn("Extras");
                    ImGui::TableSetupColumn("File size");
                    ImGui::TableSetupColumn("Resource ID");
                    ImGui::TableSetupColumn("Resman path");
                    ImGui::TableHeadersRow();

                    for (const auto& asset : _d->assets)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(asset.kind.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(asset.identifier.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(asset.dimensions.c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(asset.extras.c_str());
                        ImGui::TableSetColumnIndex(4);
                        if (asset.size == std::string::npos)
                            ImGui::TextUnformatted("unknown");
                        else
                            ImGui::Text("%zu B", asset.size);
                        ImGui::TableSetColumnIndex(5);
                        ImGui::Text("0x%08llx", static_cast<unsigned long long>(asset.resource_id));
                        ImGui::TableSetColumnIndex(6);
                        ImGui::TextUnformatted(asset.path.c_str());
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
            ImGui::End(); });
    }
    engine::instance().debug_action_register("Lupi Debug", []()
                                             {
        if (auto* m = uim())
            m->toggle_tool_window("lupi_debug"); }, 10);

    // Optional convenience: auto-start a cart configured directly on the
    // system (e.g. `lupi: { cart: ... }`). Not required — carts are normally
    // started/stopped at runtime via the RTTI-exposed lupi_start()/lupi_stop()
    // Lua functions, e.g. from a scene's own script.
    if (cfg.has_child("cart"))
    {
        std::string cart_path;
        cfg["cart"] >> cart_path;
        if (!start(cart_path))
            log::warn("[lupi] configured cart '%s' failed to start at init", cart_path.c_str());
    }

    log::info("[lupi] ready (idle)");
    return true;
}

bool lupi::start(const std::string &cart_path)
{
    stop();

    auto cart_id = entt::hashed_string{cart_path.c_str()}.value();
    auto cart = rman().get<rlupi_cart>(cart_id);
    if (!cart || !cart->valid)
    {
        log::error("[lupi] failed to load cart '%s'", cart_path.c_str());
        return false;
    }
    _d->cart = cart;
    _d->cart_dir = cart->dir;
    _d->sprite_by_name.clear();
    _d->assets.clear();

    _d->fb = lupi_framebuffer{};
    _d->pal = lupi_palette{};
    _d->gfx = lupi_gfx_state{};
    _d->frame_counter = 0;
    _d->simulation_last_counter = SDL_GetPerformanceCounter();
    _d->simulation_accumulator = 0.0;

    // Palette index 0 is the transparency sentinel (matches lupi-codec's own
    // Palette[1] = "0x0000" — see colors_manager.lua) — seed it explicitly so
    // it counts as "allocated" for lupi_scan_cart_assets' Palette synthesis.
    _d->pal.set(0, 0x0000);

    // --- lua_State: own, isolated sandbox — never shared with script_lua ---
    _d->L = luaL_newstate();
    if (!_d->L)
    {
        log::error("[lupi] luaL_newstate failed");
        _d->cart = nullptr;
        return false;
    }
    luaL_openlibs(_d->L);
    lua_pushlightuserdata(_d->L, _d);
    lua_setfield(_d->L, LUA_REGISTRYINDEX, k_registry_key);

    lupi_register_io(_d->L, *_d);
    lupi_register_ui(_d->L);
    lupi_register_input(_d->L);
    lupi_register_stubs(_d->L);
    lupi_register_print(_d->L);
    lupi_register_require(_d->L, *_d);

    // Game sources ship no asset manifest at all. We auto-discover every .png
    // under the cart directory (Sprites/Palette globals) and compile every
    // Tiled map JSON (satisfies "maps.<name>" requires) before running any
    // cart Lua, since game.lua's very first lines depend on both.
    // Basically, we need to do lupi-codec's work :)
    lupi_scan_cart_assets(_d->L, *_d);
    lupi_compile_cart_maps(_d->L, *_d);

    // --- run the cart's game.lua top level (defines update(), etc) ---
    if (luaL_loadbuffer(_d->L, _d->cart->main_lua_src.c_str(), _d->cart->main_lua_src.size(),
                        _d->cart->chunkname.c_str()) != LUA_OK ||
        lua_pcall(_d->L, 0, 0, 0) != LUA_OK)
    {
        log::error("[lupi] error loading cart '%s': %s", _d->cart->chunkname.c_str(), lua_tostring(_d->L, -1));
        lua_pop(_d->L, 1);
        lua_close(_d->L);
        _d->L = nullptr;
        _d->cart = nullptr;
        return false;
    }

    // --- show it in the default scene as an ordinary sprite ---
    if (!_d->screen_tex)
    {
        _d->screen_tex = std::make_shared<rtexture>(entt::hashed_string{"lupi_screen_texture"}.value());
        _d->screen_tex->tex = static_cast<SDL_Texture *>(_d->tex);
        _d->screen_tex->uploaded = true; // skip render_simple's lazy surf->tex upload — already have a live texture
        _d->screen_sprite = std::make_shared<rsprite>(entt::hashed_string{"lupi_screen_sprite"}.value());
        _d->screen_sprite->tex = _d->screen_tex;
    }
    auto &reg = engine::instance().default_scene().registry();
    _d->screen_entity = reg.create();
    auto &sp = reg.emplace<cspatial>(_d->screen_entity);
    sp.pos = {0.f, 0.f, 0.f};
    sp.apply();
    auto &spr = reg.emplace<csprite>(_d->screen_entity);
    spr.spr = _d->screen_sprite;

    _d->running = true;
    log::info("[lupi] started — cart '%s'", _d->cart->chunkname.c_str());
    return true;
}

void lupi::stop()
{
    if (_d->L)
    {
        lua_close(_d->L);
        _d->L = nullptr;
    }
    if (_d->screen_entity != entt::null)
    {
        auto &reg = engine::instance().default_scene().registry();
        if (reg.valid(_d->screen_entity))
            reg.destroy(_d->screen_entity);
        _d->screen_entity = entt::null;
    }
    if (_d->running)
        log::info("[lupi] stopped");
    _d->running = false;
    _d->cart = nullptr;
    _d->simulation_last_counter = 0;
    _d->simulation_accumulator = 0.0;
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
    if (phase == GENERAL_UPDATE && _d->running)
    {
        Uint64 t0 = SDL_GetPerformanceCounter();

        Uint64 now = SDL_GetPerformanceCounter();
        if (_d->fixed_rate_enabled)
        {
            constexpr double kSimulationStep = 1.0 / 60.0;
            constexpr double kMaximumElapsed = 0.25;
            double elapsed = double(now - _d->simulation_last_counter) / double(SDL_GetPerformanceFrequency());
            _d->simulation_last_counter = now;
            _d->simulation_accumulator += std::min(elapsed, kMaximumElapsed);
        }
        else
        {
            _d->simulation_last_counter = now;
            _d->simulation_accumulator = 0.0;
        }

        _d->btn.pressure_last_frame = _d->btn.pressure_this_frame;
        _d->btn.pressure_this_frame = {};

        if (auto *in = ins())
        {
            // Primary path: `input`'s own default action map already covers
            // both gamepad and keyboard — see k_lupi_gp_buttons above for why
            // this doubles as the "virtual SNES controller" the real Lupi
            // console's own input maps onto (see doc/system_lupi.md).
            for (const auto &b : k_lupi_gp_buttons)
            {
                _d->btn.pressure_this_frame[0][b.button_id] = in->action_is_pressed(b.action_id.value()) ? 255 : 0;
                if (in->action_was_pressed(b.action_id.value()))
                    _d->btn.pending_pressed[0][b.button_id] = true;
            }

            glm::vec3 dir = in->action_direction(k_lupi_gp_dir.value());
            constexpr float kDirThreshold = 0.5f;                                 // digital-from-analog trigger point
            _d->btn.pressure_this_frame[0][0] = dir.x < -kDirThreshold ? 255 : 0; // LEFT
            _d->btn.pressure_this_frame[0][1] = dir.x > kDirThreshold ? 255 : 0;  // RIGHT
            _d->btn.pressure_this_frame[0][2] = dir.y < -kDirThreshold ? 255 : 0; // UP
            _d->btn.pressure_this_frame[0][3] = dir.y > kDirThreshold ? 255 : 0;  // DOWN
        }

        auto run_update = [&]()
        {
            lua_getglobal(_d->L, "update");
            lua_pushinteger(_d->L, (lua_Integer)_d->frame_counter);
            if (lua_pcall(_d->L, 1, 0, 0) != LUA_OK)
            {
                log::error("[lupi] update() error in '%s': %s", _d->cart->chunkname.c_str(), lua_tostring(_d->L, -1));
                lua_pop(_d->L, 1);
            }
            ++_d->frame_counter;
            for (auto &player : _d->btn.pending_pressed)
                player.fill(false);
        };

        int simulation_steps = 0;
        constexpr double kSimulationStep = 1.0 / 60.0;
        constexpr int kMaximumCatchUpSteps = 5;
        const double snap_tolerance = std::clamp(_d->fixed_rate_snap_tolerance, 0.0, kSimulationStep * 0.5);
        if (_d->simulation_accumulator <= snap_tolerance)
            _d->simulation_accumulator = 0.0;
        else if (_d->simulation_accumulator < kSimulationStep &&
                 kSimulationStep - _d->simulation_accumulator <= snap_tolerance)
            _d->simulation_accumulator = kSimulationStep;
        while (_d->fixed_rate_enabled && _d->simulation_accumulator >= kSimulationStep && simulation_steps++ < kMaximumCatchUpSteps)
        {
            run_update();
            _d->simulation_accumulator -= kSimulationStep;
        }
        if (_d->fixed_rate_enabled && simulation_steps == kMaximumCatchUpSteps && _d->simulation_accumulator >= kSimulationStep)
        {
            // Drop an excessive backlog after a pause or a long hitch. This
            // keeps the game responsive instead of running stale simulation.
            _d->simulation_accumulator = 0.0;
        }
        if (!_d->fixed_rate_enabled)
            run_update();

        Uint64 t1 = SDL_GetPerformanceCounter();
        double ms = double(t1 - t0) * 1000.0 / double(SDL_GetPerformanceFrequency());
        _d->last_step_ms = ms;
        double cpu_pct = std::min(100.0, ms / (1000.0 / 60.0) * 100.0);
        _d->cpu_ema = _d->cpu_ema * 0.9 + cpu_pct * 0.1;
        double fps = ms > 0.0 ? 1000.0 / ms : 60.0;
        _d->fps_ema = _d->fps_ema * 0.9 + std::min(fps, 60.0) * 0.1;
    }

    if (phase == RENDER)
    {
        // Palette index 0 always renders fully transparent, regardless of
        // whatever color ui.palset(0, ...) has set it to — confirmed against
        // the real engine's own get_palette_color() (lupinho/src/ui.c),
        // which hardcodes this special case rather than treating it as an
        // ordinary paletted color, and is independent of alpha in the raw
        // BGR555 encoding itself (there is none).
        for (size_t i = 0; i < _d->fb.pixels.size(); ++i)
        {
            uint8_t idx = _d->fb.pixels[i];
            _d->rgba_scratch[i] = idx == 0 ? 0u : lupi_palette::bgr555_to_rgba8888(_d->pal.bgr555[idx]);
        }
        if (auto *r = rs(); r && _d->tex)
            r->update_texture(_d->tex, _d->rgba_scratch.data(), LUPI_SCREEN_W * 4);
    }

    return true;
}

bool lupi::event(SDL_Event *ev)
{
    if (!_d->text_input_started)
    {
        if (SDL_Window *win = SDL_GetKeyboardFocus())
        {
            SDL_StartTextInput(win);
            _d->text_input_started = true;
        }
    }

    switch (ev->type)
    {
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
            .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource>
            {
                return rloader_lupi_cart{}(id);
            }});
}
