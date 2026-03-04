#include <newbase/script_lua/script_lua.hpp>
#include <newbase/script_lua/lua.hpp>
#include <newbase/script_lua/bindings_glm.hpp>
#include <newbase/script_lua/utility.hpp>
#include <newbase/components/script.hpp>
#include <newbase/scene.hpp>
#include <newbase/engine.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/entt.hpp>
#include <vector>

using namespace nb;
using entt::operator""_hs;

typedef std::pair<size_t, std::vector<char>*> reader_state_t;

const char* _lua_batch_reader(lua_State* lua_state, void* reader_state, size_t* size);
int _lua_panic(lua_State * L);


struct nb::script_lua_p {
    lua_State * L {nullptr};
    unsigned int seed {0};
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
    _d->L = lua_newstate(&l_alloc, this, ++(_d->seed));
    lua_atpanic( _d->L, &_lua_panic );
    luaL_openlibs(_d->L);

    lua_newtable(_d->L);
    lua_setglobal(_d->L, "_meta");

    bind_meta_types();
    bind_systems();
    
    log::info("[script_lua] initialized");
    return true;

}

void script_lua::bind_meta_types()
{
    lua::stack_guard _guard {_d->L};

    using rtti::type_info;

    log::info("[script_lua] binding types");

    // push global meta table onto stack
    lua_getglobal(_d->L, "_meta");

    // iterate over registered entt::meta types
    for (const auto&& [id, type] : entt::resolve())
    {
        lua::stack_guard _guard {_d->L};
        
        auto name_sv = type.info().name();
        log::info("[script_lua] found meta type: %.*s (%x)", name_sv.size(), name_sv.data(), type.id());

        // create lua table for this typeinfo and store it in the registry, indexed by type id
        lua_newtable(_d->L);
        lua_pushvalue(_d->L, -1);
        lua_setfield(_d->L, LUA_REGISTRYINDEX, std::to_string(type.id()).c_str());

        // set type name in typeinfo table
        lua_pushlstring(_d->L, name_sv.data(), name_sv.size());
        lua_setfield(_d->L, -2, "typename");

        // set type id in typeinfo table
        lua_pushinteger(_d->L, type.id());
        lua_setfield(_d->L, -2, "id");

        // check if metatype has custom identifier set
        const rtti::type_info * t_info = type.custom();
        if(t_info)
        {
            log::info("[script_lua] found type info: %s", (const char*)t_info->identifier);

            // set type identifier in typeinfo table
            lua_pushstring(_d->L, t_info->identifier);
            lua_setfield(_d->L, -2, "identifier");
        }
        else
        {
            // auto-determine type identifier
            std::string identifier = _util_auto_identifier(name_sv);
            log::info("[script_lua] no type info, auto identifier: %s", identifier.c_str());
    
            // set type identifier in typeinfo table
            lua_pushlstring(_d->L, identifier.c_str(), identifier.size());
            lua_setfield(_d->L, -2, "identifier");
        }

        // iterate over functions in type
        int func_idx = 0;
        for(const auto &&func_pair : type.func())
        {   
            // TODO need to check all overloads
            const entt::meta_func &func = func_pair.second;
            const rtti::func_info *func_info = func.custom();
            if(!func_info)
            {
                log::warn("[script_lua] skipping function with no info: %d args, const: %d, static: %d", func.arity(), func.is_const(), func.is_static());
                ++func_idx;
                continue;
            }
            log::info("[script_lua] found function with index: %d, name: %s", func_idx, func_info->identifier);
            ++func_idx;
        }

        // iterate over data members in type
        // --

        // save table ref to metatype table and pop from stack
        lua_seti(_d->L, -2, type.id());
    }

    // pop global meta table from stack
    lua_pop(_d->L, 1);
}

void script_lua::bind_systems()
{
    auto system_t = entt::resolve<nb::system>();
    for (auto&& [id, type] : entt::resolve())
    {
        if (type.can_cast(system_t))
        {
            // this is a system
            const rtti::type_info *info = type.custom();
            if(!info)
            {
                log::warn("[script_lua] skipping system with no info: %x", type.id());
                continue;
            }
            if(info->type_class != rtti::TYPE_CLASS_SYSTEM)
            {
                log::warn("[script_lua] skipping system with wrong type class: %s (%x)", (const char*)info->identifier, type.id());
                continue;
            }
            auto sys = engine::instance().system_from_id(type.id());
            if(!sys)
            {
                log::warn("[script_lua] skipping system with no instance: %s (%x)", (const char*)info->identifier, type.id());
                continue;
            }
            log::warn("[script_lua] UNIMPLEMENTEND binding system: %s (%x)", (const char*)info->identifier, type.id());
            /*

            if(sys->can_bind())
            {
                log::info("[script_lua] requesting bind: %s (%x)", (const char*)info->identifier, type.id());
                sys->bind(_d->L);
            }
            else
            {
                log::warn("[script_lua] system cannot bind: %s (%x)", (const char*)info->identifier, type.id());
            }
            */
        }
    }

}

bool script_lua::step(step_phase phase)
{
    // TODO do not scan everything every frame (use reactive storage)
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

                    // prepate the meta
                    // prepare_env();

                    /*sol::state_view lua{_d->L};
                    sol::protected_function f(_d->L, lua.stack_top());
                    sol::environment env {lua, sol::create, lua.globals()};
                    env.set_on(f);

                    env.set("eid", id);
                    
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
                        */
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

// this abomination attempts to convert a C++ type name into a more lua-friendly identifier, by removing namespaces and replacing :: with _
// it should also include first-template-parameter digits into the names, for glm::vec2, glm::vec3, etc
std::string script_lua::_util_auto_identifier(const std::string_view &identifier)
{
    // remove first namespace if exists, and convert :: to _ (for nested types)
    size_t start = 0;
    size_t end = identifier.size();
    for(size_t i = 0; i < identifier.size(); ++i)
    {
        if(identifier[i] == ':' && i + 1 < identifier.size() && identifier[i+1] == ':')
        {
            start = i + 2;
            break;
        }
    }
    std::string result;
    for(size_t i = start; i < end; ++i)
    {
        if(identifier[i] == ':' && i + 1 < identifier.size() && identifier[i+1] == ':')
        {
            result += '_';
            ++i;
        }
        else
            result += identifier[i];
    }

    // remove everything after and including first < if exists (for template types)
    // if first character after < is a digit, it must be appended to the identifier (for template types with non-type parameters)
    size_t template_start = result.find('<');
    char digit_to_add = '\0';
    if(template_start != std::string::npos)
    {
        if(template_start + 1 < result.size() && std::isdigit(result[template_start + 1]))
        {
            digit_to_add = result[template_start + 1];
        }
        result = result.substr(0, template_start);
    }

    if(digit_to_add != '\0')
        result += digit_to_add;

    return result;
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


// custom panic function
int _lua_panic(lua_State * L)
{
    const char* error_message = lua_tostring(L, -1);
    log::critical("[script_lua] PANIC: '%s'", error_message);
    abort();
    return 0;
}


// RTTI metadata
extern "C" void _rtti_init_script_lua()
{
    entt::meta_factory<script_lua>{}
        .type("script_lua"_hs)
        .custom<rtti::type_info>(rtti::type_info{"script_lua", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::script_lua>>{rtti::ctx_systems()}
        .type("script_lua_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::script_lua>>()
        .conv<std::shared_ptr<nb::system>>();

    cscript::_ensure_rtti();
}
