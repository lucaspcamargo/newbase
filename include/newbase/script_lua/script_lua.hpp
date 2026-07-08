#pragma once

#include <newbase/system.hpp>

namespace nb {

struct script_lua_p;

class script_lua final : public system {
public:
    script_lua();
    ~script_lua();
    

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override {return 0;}
    entt::id_type metatype_id() override { return entt::hashed_string{"script_lua"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;
    void on_scene_change() override;

    // Compiles and runs an arbitrary chunk of Lua code on the main thread
    // (same lua_State used for scripted entities/systems — full access to
    // whatever the game's Lua bindings expose). Returns the stringified
    // first return value of the chunk (via the __tostring-aware
    // luaL_tolstring, so boxed nb.meta_any results print like the REPL
    // would), or an empty string if the chunk returned nothing. On a
    // load/runtime error, the error is logged and a "load error: "/
    // "runtime error: " prefixed message is returned instead (this engine
    // has no exception-based script error propagation precedent — see
    // step()'s script chunk execution, which follows the same log+return
    // convention).
    std::string eval(const std::string &code);

private:
    void bind_meta_types();
    void bind_systems();
    void bind_services();
    void bind_component_getters();
    void bind_resource_getters();
    void bind_global_api();

    // our own allocator provided to lua
    static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize);

    static void _on_cscript_destroy(entt::registry &reg, entt::entity eid);

    static std::string _util_auto_identifier(const std::string_view &identifier);

    script_lua_p *_d {nullptr};
};

}
