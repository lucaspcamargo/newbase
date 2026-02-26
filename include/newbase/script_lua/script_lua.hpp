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

private:
    void bind_meta_types();
    void bind_systems();

    // our own allocator provided to lua
    static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize);

    static std::string _util_auto_identifier(const std::string_view &identifier);

    script_lua_p *_d {nullptr};
};

}
