#pragma once

#include <newbase/system.hpp>
#include <string>

namespace nb {

struct lupi_p;

// Compatibility layer for games written against the Lupi console API
// (lupi.api.br/docs — 480x270, 256-color indexed RGB555 palette, single
// update(frame) callback). Loads a "cart" (a resource directory with a
// game.lua entry script + assets) into a dedicated, sandboxed lua_State and drives it by calling
// update(frame) once per simulation frame, mirroring the console's
// single-callback model. Unlike src/sgdk (a compat layer for real, translated
// SGDK C code), lupi carts are pure Lua with a single non-blocking per-frame
// callback, so no separate thread is needed — everything runs synchronously
// on the host thread from step().
//
// No cart runs by default: init() only sets up the persistent GPU texture and
// debug tool window. start()/stop()/running() are RTTI-registered (see
// _rtti_init_lupi in lupi.cpp), so script_lua exposes them as
// lupi_start(cart_path)/lupi_stop()/lupi_running() — a scene's own Lua script
// drives the "arcade cabinet" lifecycle. While running, start() also creates
// a csprite entity in the default scene showing the live framebuffer; stop()
// (and on_scene_change(), which calls it automatically) removes it.
class lupi : public system
{
public:
    lupi();
    ~lupi();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"lupi"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;
    void on_scene_change() override;

    // Boots `cart_path` (a .lupicart manifest resource path) into a fresh
    // lua_State, replacing any currently running cart, and shows it as a
    // sprite in the default scene. Returns false on load failure.
    bool start(const std::string& cart_path);

    // Tears down the running cart's lua_State and removes its scene sprite.
    // Safe to call when nothing is running.
    void stop();

    bool running() const;

private:
    lupi_p *_d;
};

}
