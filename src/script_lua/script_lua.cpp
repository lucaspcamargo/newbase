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

#include <vector>

using namespace nb;

typedef std::pair<size_t, std::vector<char>*> reader_state_t;

const char* _lua_batch_reader(lua_State* lua_state, void* reader_state, size_t* size);


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
    // TODO maybe not scan everything every frame (use reactive storage)
    if(phase == step_phase::PREPARE)
    {
        auto view = reg().view<cscript>();
        for (auto [id, script]: view.each())
        {
            auto &script_res = script.script;
            if(script.ready || script.skip)
                continue;

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[script_lua] preparing: %x", id);
            if(script_res->valid)
            {
                if(script.state == nullptr)
                {
                    // create new lua state if needed
                    script.state = lua_newstate(&l_alloc, this);
                    luaL_openlibs(script.state);    // TODO make optional?
                    // TODO prepare global table
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[script_lua] new state: %x", id);
                }
                bool valid_chunk = false;
                if(script_res->type == script_type::LUA_SOURCE)
                {
                    // lets parse this
                    reader_state_t state = std::make_pair<size_t, std::vector<char>*>(0, &(script_res->raw));
                    int load_ret = lua_load(script.state, &_lua_batch_reader, &state, "_unnamed_chunk", nullptr);

                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[script_lua] loaded %x, ret: %d", id, load_ret);

                    if(load_ret == LUA_OK)
                    {
                        // TODO all ok, dump bytecode back into resource and change type
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[script_lua] loaded: %x", id);
                        valid_chunk = true;
                    }else if(load_ret == LUA_ERRSYNTAX)
                    {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[script_lua] syntax error: %s", lua_tostring(script.state, -1));
                        lua_pop(script.state, 1);
                        script_res->valid = false;
                    }
                    else
                    {
                        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "[script_lua] can't handle load, aborting");
                        assert(0);
                    }
                }
                else
                {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[script_lua] unknown type: %d, skipping", script_res->type);
                    script.skip = true;
                }
                if(valid_chunk)
                {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[script_lua] setup: %x", id);
                    lua_call(script.state, 0, 0);
                    script.ready = true;
                }
            }
            else
            {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[script_lua] invalid: %x, skipping", id);
                script.skip = true;
            }
        }
    }
    return true;
}

bool script_lua::event(SDL_Event*) 
{
    return true;
}

void* script_lua::l_alloc (void *ud, void *ptr, size_t osize, size_t nsize)
{
    // TODO use a pool allocator? entt provides some stuff
    (void) ud;  (void) osize;  /* unused */
    if (nsize == 0) {
        free(ptr);
        return NULL;
    }
    else
        return realloc(ptr, nsize);
}

const char* _lua_batch_reader(lua_State* lua_state, void* reader_state, size_t* size)
{
    (void) lua_state;
    reader_state_t &state = *(reinterpret_cast<reader_state_t*>(reader_state));
    const auto all_sz = state.second->size();
    if(state.first < all_sz)
    {
        // return a chunk, which in this case is the whole data array
        const auto returned_sz = all_sz - state.first;
        (*size) = returned_sz;
        const auto ret = state.second->data() + state.first;
        state.first += returned_sz;
        return ret;
    }
    return nullptr;
}
