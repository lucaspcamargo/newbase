#include <newbase/script_lua/script_lua.h>
#include <newbase/components/script.h>
#include <newbase/scene.h>
#include <newbase/engine.h>
#include <newbase/reflection/contexts.h>
#include <newbase/reflection/data.h>
#include <newbase/script_lua/lua.h>
#include <newbase/script_lua/bindings_glm.h>
#include <newbase/log.h>
#include <sol/sol.hpp>
#include <entt/entt.hpp>
#include <vector>

using namespace nb;
using entt::operator""_hs;

typedef std::pair<size_t, std::vector<char>*> reader_state_t;

const char* _lua_batch_reader(lua_State* lua_state, void* reader_state, size_t* size);
void _lua_panic(sol::optional<std::string> maybe_msg);


struct nb::script_lua_p {
    lua_State * L {nullptr};
    std::unordered_map<entt::id_type, rtti::component_type_info::bind_result> bound_components; // map entt component types to sol usertypes
};


script_lua::script_lua()
{
    _d = new script_lua_p();
}

script_lua::~script_lua()
{
    delete _d;
}

bool script_lua::init(ryml::ConstNodeRef cfg)
{
    log::info("[script_lua] init");
    _d->L = lua_newstate(&l_alloc, this);
    lua_atpanic( _d->L, sol::c_call<decltype(&_lua_panic), &_lua_panic> );
    luaL_openlibs(_d->L);

    _lua_bind_glm(sol::state_view{_d->L});
    bind_engine();
    bind_systems();
    
    log::info("[script_lua] initialized");
    return true;

}

void script_lua::bind_engine()
{
    sol::state_view lua{_d->L};

    lua.set_function("hs", [](const char * str) -> int {
        return entt::hashed_string{str}.value();
    });

    auto registry_lua_t = lua.new_usertype<::entt::registry>("registry"
        "new", sol::no_constructor);
        registry_lua_t["orphan"] = &::entt::registry::orphan;
        registry_lua_t["clear"] = &::entt::registry::clear;

    auto scene_lua_t = lua.new_usertype<::nb::scene>("scene",
        "new", sol::no_constructor,
        "registry", &scene::registry);

    auto engine_lua_t = lua.new_usertype<::nb::engine>("engine", 
        "new", sol::no_constructor,
        "ref", &engine::instance,
        "default_scene", &engine::default_scene,
        "request_exit", &engine::request_exit);
}

void script_lua::bind_systems()
{
    auto system_t = entt::resolve<nb::system>();
    for (auto&& [id, type] : entt::resolve())
    {
        if (type.can_cast(system_t))
        {
            // this is a system
            const rtti::system_info *info = type.custom();
            if(!info)
            {
                log::warn("[script_lua] skipping system with no info: %x", type.id());
                continue;
            }
            auto sys = engine::instance().system_from_id(type.id());
            if(!sys)
            {
                log::warn("[script_lua] skipping system with no instance: %s (%x)", (const char*)info->identifier, type.id());
                continue;
            }
            if(sys->can_bind())
            {
                log::info("[script_lua] requesting bind: %s (%x)", (const char*)info->identifier, type.id());
                sys->bind(_d->L);
            }
            else
            {
                log::warn("[script_lua] system cannot bind: %s (%x)", (const char*)info->identifier, type.id());
            }
        }
    }

}

bool script_lua::step(step_phase phase)
{
    // TODO maybe not scan everything every frame (use reactive storage)
    if(phase == step_phase::PREPARE)
    {
        auto &reg = engine::instance().default_scene().registry();
        auto view = reg.view<cscript>();
        for (auto [id, script]: view.each())
        {
            auto &script_res = script.script;
            if(script.ready || script.skip)
                continue;

            log::info("[script_lua] preparing: %x", id);
            if(script_res->valid)
            {
                if(script.state == nullptr)
                {
                    // associate lua state to script component
                    // well, this may not be necessary, and may be even dangerous
                    // better to store reference to script environment
                    script.state = _d->L;
                }
                bool valid_chunk = false;
                if(script_res->type == script_type::LUA_SOURCE)
                {
                    // lets parse this
                    reader_state_t state = std::make_pair<size_t, std::vector<char>*>(0, &(script_res->raw));
                    int load_ret = lua_load(script.state, &_lua_batch_reader, &state, "_unnamed_chunk", nullptr);

                    log::info("[script_lua] loaded %x, ret: %d", id, load_ret);

                    if(load_ret == LUA_OK)
                    {
                        // TODO all ok, dump bytecode back into resource and change type
                        log::info("[script_lua] loaded: %x", id);
                        valid_chunk = true;
                    }else if(load_ret == LUA_ERRSYNTAX)
                    {
                        log::error("[script_lua] syntax error: %s", lua_tostring(script.state, -1));
                        lua_pop(script.state, 1);
                        script_res->valid = false;
                    }
                    else
                    {
                        log::critical("[script_lua] can't handle load, aborting");
                        assert(0);
                    }
                }
                else
                {
                    log::info("[script_lua] unknown type: %d, skipping", script_res->type);
                    script.skip = true;
                }
                if(valid_chunk)
                {
                    log::info("[script_lua] setup: %x", id);
                    sol::state_view lua{_d->L};
                    sol::protected_function f(_d->L, lua.stack_top());
                    sol::environment env {lua, sol::create, lua.globals()};
                    env.set_on(f);
                    
                    // check which components to make available
                    for(auto&& curr : reg.storage())
                    {
                        if(auto& storage = curr.second; storage.contains(id))
                        {
                            entt::id_type comp_id = curr.first;
                            log::info("[script_lua] entity %x has component %x", id, comp_id);
                            auto it = _d->bound_components.find(comp_id);
                            const char *userdata_id = nullptr;
                            if(it == _d->bound_components.end())
                            {
                                // haven't bound the component before
                                log::info("[script_lua] needs registration");
                                auto comp_type = entt::resolve(comp_id);
                                log::info("[script_lua] meta type info: %s", std::string(comp_type.info().name()).c_str());
                                if(comp_type.info() == entt::type_id<void>())
                                {
                                    log::warn("[script_lua] unregistered component: %x", comp_id);
                                    continue;
                                }
                                rtti::component_type_info *info = comp_type.custom();
                                if(!info)
                                {
                                    log::error("[script_lua] component has no info: %x", comp_id);
                                    continue;
                                }
                                auto bind_result = info->_bind_func(_d->L);
                                _d->bound_components[comp_id] = bind_result;
                                userdata_id = bind_result.first;
                            }
                            else
                                userdata_id = it->second.first;

                            auto mt = lua[userdata_id].get<sol::metatable>();
                            assert(mt.valid());
                            assert(storage.value(id));
                            _d->bound_components[comp_id].second(&env, id, reg);
                            // we have asked the binding to add the component to the current lua state
                        }
                    }

                    sol::protected_function_result result = f();
                    if (result.valid()) {
                        log::info("[script_lua] script ok: %x", id);
                    }
                    else {
                        sol::error err = result;
                        log::error("[script_lua] script failed: %x: %s", id, err.what());
                    }
                    script.ready = true;
                }
            }
            else
            {
                log::info("[script_lua] invalid: %x, skipping", id);
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


inline void _lua_panic(sol::optional<std::string> maybe_msg) {
    if (maybe_msg) {
        const std::string& msg = maybe_msg.value();
        log::critical("[script_lua] panic: %s", msg.c_str());
	}
    else
        log::critical("[script_lua] panic");
}



// RTTI metadata
extern "C" void _rtti_init_script_lua()
{
    entt::meta_factory<script_lua>{}
        .type("script_lua"_hs)
        .custom<rtti::system_info>(rtti::system_info{"script_lua"})
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::script_lua>>{rtti::ctx_systems()}
        .type("script_lua_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::script_lua>>()
        .conv<std::shared_ptr<nb::system>>();

    cscript::_ensure_rtti();
}