#include <newbase/script_lua/script_lua.h>
#include <newbase/components/script.h>
#include <newbase/ecs.h>

#include <SDL3/SDL_log.h>
#include <entt/entt.hpp>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

using namespace nb;


SDL_InitFlags script_lua::sdl_subsystems()
{
    return 0;
}

bool script_lua::init(int argc, char **argv)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[script_lua] init");
    return true;
}

bool script_lua::step(step_phase phase)
{
    // TODO maybe not scan everything
    if(phase == step_phase::PREPARE)
    {
        auto view = reg().view<cscript>();
        for (auto [id, script]: view.each())
        {
            auto &script_res = script.script;
            if(script_res->valid)
            {
                if(script.state == nullptr)
                {
                    script.state = luaL_newstate();
                }
                if(script_res->state == script_state::UNPARSED_SOURCE && script_res->type == script_type::LUA_SOURCE)
                {
                    // lets parse this
                    
                }
            }
        }
    }
    return true;
}

bool script_lua::event(SDL_Event*) 
{
    return true;
}
