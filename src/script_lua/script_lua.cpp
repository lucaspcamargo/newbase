#include <newbase/script_lua/script_lua.hpp>
#include <newbase/script_lua/lua.hpp>
#include <newbase/script_lua/bindings.hpp>
#include <newbase/script_lua/bindings_glm.hpp>
#include <newbase/script_lua/utility.hpp>
#include <newbase/components/script.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/scene.hpp>
#include <newbase/engine.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/i18n/i18n.hpp>
#include <newbase/log.hpp>
#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>

using namespace nb;
using entt::operator""_hs;

typedef std::pair<size_t, std::vector<char>*> reader_state_t;

const char* _lua_batch_reader(lua_State* lua_state, void* reader_state, size_t* size);
int _lua_panic(lua_State * L);


struct nb::script_lua_p {
    lua_State * L {nullptr};
    unsigned int seed {0};
    // per-entity list of luaL_ref handles to destroy callbacks
    std::unordered_map<entt::entity, std::vector<int>> on_destroy_callbacks;
};


script_lua::script_lua()
{
    _d = new script_lua_p();
}

script_lua::~script_lua()
{
    log::info("[script_lua] destroying");
    lua_close(_d->L);
    delete _d;
    log::info("[script_lua] destroyed");
}

void script_lua::_on_cscript_destroy(entt::registry &reg, entt::entity eid)
{
    auto sys_shared = engine::instance().system_from_id("script_lua"_hs);
    if (!sys_shared) return;
    auto *sys = static_cast<script_lua*>(sys_shared.get());
    auto *d = sys->_d;
    auto it = d->on_destroy_callbacks.find(eid);
    if (it == d->on_destroy_callbacks.end()) return;
    for (int ref : it->second)
    {
        lua_rawgeti(d->L, LUA_REGISTRYINDEX, ref);
        if (lua_isfunction(d->L, -1))
        {
            if (lua_pcall(d->L, 0, 0, 0) != LUA_OK)
            {
                const char *err = lua_tostring(d->L, -1);
                log::error("[script_lua] script_on_destroy error for entity %x: %s", eid, err ? err : "?");
                lua_pop(d->L, 1);
            }
        }
        else
        {
            lua_pop(d->L, 1);
        }
        luaL_unref(d->L, LUA_REGISTRYINDEX, ref);
    }
    d->on_destroy_callbacks.erase(it);

    // unref the env table
    auto *sc = reg.try_get<cscript>(eid);
    if (sc && sc->env_ref != LUA_NOREF)
        luaL_unref(d->L, LUA_REGISTRYINDEX, sc->env_ref);
}

bool script_lua::init(ryml::ConstNodeRef cfg)
{
    log::info("[script_lua] init");
    _d->L = lua_newstate(&l_alloc, this, ++(_d->seed));
    lua_atpanic( _d->L, &_lua_panic );
    luaL_openlibs(_d->L);

    // connect cscript destroy signal so we can fire per-entity cleanup callbacks
    engine::instance().default_scene().registry().on_destroy<cscript>().connect<&_on_cscript_destroy>();

    lua_newtable(_d->L);
    lua_setglobal(_d->L, "_meta");

    lua::register_box_metatable(_d->L);
    bind_meta_types();
    bind_systems();
    bind_services();
    bind_component_getters();
    bind_resource_getters();
    bind_global_api();
    
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
        std::string identifier;
        if(t_info)
        {
            log::info("[script_lua] found type info: %s", (const char*)t_info->identifier);
            identifier = t_info->identifier;
        }
        else
        {
            // auto-determine type identifier
            identifier = _util_auto_identifier(name_sv);
            log::info("[script_lua] no type info, auto identifier: %s", identifier.c_str());
        }

        // set type identifier in typeinfo table
        lua_pushlstring(_d->L, identifier.c_str(), identifier.size());
        lua_setfield(_d->L, -2, "identifier");

        // register global type table with constructor: e.g. vec2.new(x, y)
        {
            entt::id_type type_id = type.id();
            lua_newtable(_d->L);
            lua_pushinteger(_d->L, (lua_Integer)type_id);
            lua_pushcclosure(_d->L, [](lua_State *L) -> int {
                auto tid = (entt::id_type)lua_tointeger(L, lua_upvalueindex(1));
                auto mtype = entt::resolve(tid);
                int argc = lua_gettop(L);
                std::vector<entt::meta_any> args;
                args.reserve(argc);
                for (int i = 1; i <= argc; ++i)
                    args.push_back(lua::lua_to_meta_any(L, i));
                auto result = mtype.construct(args.empty() ? nullptr : args.data(), args.size());
                if (!result) { lua_pushnil(L); return 1; }
                lua::push_meta_any(L, std::move(result));
                return 1;
            }, 1);
            lua_setfield(_d->L, -2, "new");
            lua_setglobal(_d->L, identifier.c_str());
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
                log::warn("[script_lua] skipping function with no info at index: %d", func_idx);
                continue;
            }
#ifndef ANDROID
            // why this crashes android armv7 in SDL_GetLogPriority -> SDL_CheckInitLog -> SDL_GetAtomicInt --> __sync_or_and_fetch ????
            // either gets stuck in debugging or segfaults outright
            // I don't know for now :(
            // There are strange forces at play here
            log::info("[script_lua] found function with index: %d, name: %s", func_idx, func_info->identifier);
#endif
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
    for (auto&& [cpp_id, type] : entt::resolve())
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

            std::string global_name = "sys_";
            global_name += info->identifier;
            lua::push_meta_any(_d->L, type.from_void(sys.get()), sys);
            lua_setglobal(_d->L, global_name.c_str());
            log::info("[script_lua] bound system: %s", global_name.c_str());

            // register per-function globals: ${sys_name}_${func_name}()
            for (const auto&& [fhash, func] : type.func())
            {
                const rtti::func_info *func_info = func.custom();
                if (!func_info)
                    continue;

                std::string fname = std::string(info->identifier) + "_" + static_cast<const char*>(func_info->identifier);
                log::info("[script_lua] registering system function: %s", fname.c_str());

                lua_pushlightuserdata(_d->L, sys.get());
                lua_pushinteger(_d->L, (lua_Integer)type.id());
                lua_pushinteger(_d->L, (lua_Integer)fhash);
                lua_pushcclosure(_d->L, [](lua_State *L) -> int {
                    void      *ptr   = lua_touserdata(L, lua_upvalueindex(1));
                    auto       tid   = (entt::id_type)lua_tointeger(L, lua_upvalueindex(2));
                    auto       fhash = (entt::id_type)lua_tointeger(L, lua_upvalueindex(3));
                    auto mtype = entt::resolve(tid);
                    auto func  = mtype.func(fhash);
                    if (!func)
                        return luaL_error(L, "system function not found in meta (tid=%x fhash=%x)", tid, fhash);
                    auto instance = mtype.from_void(ptr);
                    int argc = lua_gettop(L);
                    std::vector<entt::meta_any> args;
                    args.reserve(argc);
                    for (int i = 1; i <= argc; ++i)
                        args.push_back(lua::lua_to_meta_any(L, i));
                    for (size_t i = 0; i < args.size() && i < func.arity(); ++i)
                        args[i] = lua::coerce_lua_function_arg(std::move(args[i]), func.arg(i));
                    auto result = func.invoke(instance, args.empty() ? nullptr : args.data(), args.size());
                    if (result) { lua::push_meta_any(L, std::move(result)); return 1; }

                    // invocation failed — build a diagnostic error
                    std::string err = "system function invocation failed\n  expected args (";
                    err += std::to_string(func.arity()) + "):";
                    for (size_t i = 0; i < func.arity(); ++i)
                        err += std::string{"\n    ["} + std::to_string(i) + "] " + std::string{func.arg(i).info().name()};
                    err += "\n  got args (";
                    err += std::to_string(args.size()) + "):";
                    for (size_t i = 0; i < args.size(); ++i)
                        err += std::string{"\n    ["} + std::to_string(i) + "] " + std::string{args[i].type().info().name()};
                    return luaL_error(L, "%s", err.c_str());
                }, 3);
                lua_setglobal(_d->L, fname.c_str());
            }
        }
    }

}

void script_lua::bind_services()
{
    for (auto&& [cpp_id, mtype] : entt::resolve())
    {
        auto *info = mtype.custom().operator rtti::type_info*();
        if (!info || info->type_class != rtti::TYPE_CLASS_SERVICE || !info->data.service.getter)
        {
            continue;
        }

        std::string gname = std::string{"svc_"} + static_cast<const char*>(info->identifier);
        log::info("[script_lua] registering service getter: %s (%x)", gname.c_str(), (int)mtype.id());

        lua_pushinteger(_d->L, (lua_Integer)mtype.id());
        lua_pushcclosure(_d->L, [](lua_State *L) -> int {
            auto  tid   = (entt::id_type)lua_tointeger(L, lua_upvalueindex(1));
            auto  mtype = entt::resolve(tid);
            if(!mtype)
            {
                log::warn("[script_lua] service '%x': invalid type id", (int) tid);
                lua_pushnil(L); 
                return 1;
            }
            auto *info  = mtype.custom().operator rtti::type_info*();
            if (!info)
            { 
                log::warn("[script_lua] service '%x': no metainfo", (int) tid);
                lua_pushnil(L); 
                return 1;
            }
            if(!info->data.service.getter) 
            {
                log::warn("[script_lua] service '%s': no service getter", info->identifier.operator const char *());
                lua_pushnil(L); 
                return 1;
            }
            void *ptr  = info->data.service.getter();
            if (!ptr) { 
                log::warn("[script_lua] service '%s': getter returned null");
                lua_pushnil(L); return 1; 
            }
            lua::push_meta_any(L, mtype.from_void(ptr));
            return 1;
        }, 1);
        lua_setglobal(_d->L, gname.c_str());
    }
}

void script_lua::bind_component_getters()
{
    for (auto&& [cpp_id, mtype] : entt::resolve())
    {
        auto *info = mtype.custom().operator rtti::type_info*();
        if (!info || info->type_class != rtti::TYPE_CLASS_COMPONENT)
            continue;

        // register get_<identifier>(eid) -> component box or nil
        lua_pushinteger(_d->L, (lua_Integer)mtype.id());
        lua_pushcclosure(_d->L, [](lua_State *L) -> int {
            auto eid  = static_cast<entt::entity>(lua_tointeger(L, 1));
            auto cid  = static_cast<entt::id_type>(lua_tointeger(L, lua_upvalueindex(1)));
            auto &reg = engine::instance().default_scene().registry();
            auto *stor = reg.storage(cid);
            if (!stor || !stor->contains(eid)) { lua_pushnil(L); return 1; }
            auto ctype = entt::resolve(cid);
            auto any   = ctype.from_void(stor->value(eid));
            if (!any) { lua_pushnil(L); return 1; }
            lua::push_meta_any(L, std::move(any));
            return 1;
        }, 1);

        std::string gname = std::string{"get_"} + static_cast<const char*>(info->identifier);
        lua_setglobal(_d->L, gname.c_str());
        log::info("[script_lua] registered component getter: %s", gname.c_str());
    }
}

void script_lua::bind_resource_getters()
{
    // Build a map: resource meta type id → ptr meta type id
    // (matches TYPE_CLASS_RESOURCE to its TYPE_CLASS_RESOURCE_PTR counterpart)
    std::unordered_map<entt::id_type, entt::id_type> res_to_ptr;
    for(auto&& [cpp_id, mtype] : entt::resolve())
    {
        auto *info = mtype.custom().operator rtti::type_info*();
        if(!info || info->type_class != rtti::TYPE_CLASS_RESOURCE_PTR)
            continue;
        res_to_ptr[info->data.resource_ptr.resource_type_id] = mtype.id();
    }

    // Register res_get_<identifier>(asset_hash) for each resource type that has a ptr type.
    for(auto&& [cpp_id, mtype] : entt::resolve())
    {
        auto *info = mtype.custom().operator rtti::type_info*();
        if(!info || info->type_class != rtti::TYPE_CLASS_RESOURCE)
            continue;

        auto it = res_to_ptr.find(mtype.id());
        if(it == res_to_ptr.end())
            continue;

        entt::id_type type_id     = mtype.id();
        entt::id_type ptr_type_id = it->second;

        lua_pushinteger(_d->L, (lua_Integer)type_id);
        lua_pushinteger(_d->L, (lua_Integer)ptr_type_id);
        lua_pushcclosure(_d->L, [](lua_State *L) -> int {
            auto asset_id  = static_cast<entt::id_type>(lua_tointeger(L, 1));
            auto type_id   = static_cast<entt::id_type>(lua_tointeger(L, lua_upvalueindex(1)));
            auto ptr_tid   = static_cast<entt::id_type>(lua_tointeger(L, lua_upvalueindex(2)));

            auto base = nb::rman().get(type_id, asset_id);
            if(!base) { lua_pushnil(L); return 1; }

            auto ptr_mtype = entt::resolve(ptr_tid);
            if(!ptr_mtype) { lua_pushnil(L); return 1; }
            auto *ptr_info = ptr_mtype.custom().operator nb::rtti::type_info*();
            if(!ptr_info || !ptr_info->data.resource_ptr.set_ptr) { lua_pushnil(L); return 1; }

            auto ptr_any = ptr_mtype.construct();
            if(!ptr_any) { lua_pushnil(L); return 1; }
            ptr_info->data.resource_ptr.set_ptr(ptr_any, base);

            lua::push_meta_any(L, std::move(ptr_any));
            return 1;
        }, 2);

        std::string gname = std::string{"res_get_"} + static_cast<const char*>(info->identifier);
        lua_setglobal(_d->L, gname.c_str());
        log::info("[script_lua] registered resource getter: %s", gname.c_str());
    }
}

void script_lua::bind_global_api()
{
    // hs(str) -> integer: entt hashed_string at runtime
    lua_pushcfunction(_d->L, [](lua_State *L) -> int {
        const char *s = luaL_checkstring(L, 1);
        lua_pushinteger(L, (lua_Integer)entt::hashed_string{s}.value());
        return 1;
    });
    lua_setglobal(_d->L, "hs");

    // engine: non-owning reference to nb::engine::instance()
    lua::push_meta_any(_d->L, entt::forward_as_meta(engine::instance()));
    lua_setglobal(_d->L, "engine");

    // entity_destroy(eid) -- queue entity for destruction at end of PREPARE
    lua_pushcfunction(_d->L, [](lua_State *L) -> int {
        auto eid = static_cast<entt::entity>(lua_tointeger(L, 1));
        engine::instance().default_scene().queue_destroy(eid);
        return 0;
    });
    lua_setglobal(_d->L, "entity_destroy");

    // entity_spawn(etree_id) -> eid -- instantiate etree, returns first entity id
    lua_pushcfunction(_d->L, [](lua_State *L) -> int {
        auto etree_id = static_cast<entt::id_type>(lua_tointeger(L, 1));
        auto eid = engine::instance().default_scene().build_etree(etree_id);
        if(eid == entt::null)
            lua_pushnil(L);
        else
            lua_pushinteger(L, static_cast<lua_Integer>(entt::to_integral(eid)));
        return 1;
    });
    lua_setglobal(_d->L, "entity_spawn");

    // script_get_env(eid) -> env table or nil
    lua_pushcfunction(_d->L, [](lua_State *L) -> int {
        auto eid = static_cast<entt::entity>(lua_tointeger(L, 1));
        auto *sc = engine::instance().default_scene().registry().try_get<cscript>(eid);
        if (!sc || sc->env_ref == LUA_NOREF) { lua_pushnil(L); return 1; }
        lua_rawgeti(L, LUA_REGISTRYINDEX, sc->env_ref);
        return 1;
    });
    lua_setglobal(_d->L, "script_get_env");

    // scene_load(etree_id) -- queue a scene change; takes effect at start of next step
    lua_pushcfunction(_d->L, [](lua_State *L) -> int {
        auto etree_id = static_cast<entt::id_type>(lua_tointeger(L, 1));
        engine::instance().request_scene_change(etree_id);
        return 0;
    });
    lua_setglobal(_d->L, "scene_load");

    // tr(msg) -> string: translate a message using the active i18n dictionary
    lua_pushcfunction(_d->L, [](lua_State *L) -> int {
        const char *msg = luaL_checkstring(L, 1);
        auto result = nb::i18n::tr(msg);
        lua_pushstring(L, result.c_str());
        return 1;
    });
    lua_setglobal(_d->L, "tr");

    // ntr(msg, msg_plural, n) -> string: translate with plural form
    lua_pushcfunction(_d->L, [](lua_State *L) -> int {
        const char *msg        = luaL_checkstring(L, 1);
        const char *msg_plural = luaL_checkstring(L, 2);
        int n                  = static_cast<int>(luaL_checkinteger(L, 3));
        auto result = nb::i18n::ntr(msg, msg_plural, n);
        lua_pushstring(L, result.c_str());
        return 1;
    });
    lua_setglobal(_d->L, "ntr");

    // set_language(lang) -- override active language, e.g. "pt_BR"
    lua_pushcfunction(_d->L, [](lua_State *L) -> int {
        const char *lang = luaL_checkstring(L, 1);
        nb::i18n::set_language(lang);
        return 0;
    });
    lua_setglobal(_d->L, "set_language");
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
                    std::string chunk_name = script_res->chunkname.empty()
                        ? "=unnamed"
                        : ("=" + script_res->chunkname);
                    int load_ret = lua_load(script.state, &_lua_batch_reader, &state, chunk_name.c_str(), nullptr);

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

                    int func_idx = lua_gettop(_d->L);

                    // create per-script environment, falling back to globals via __index
                    lua_newtable(_d->L);
                    int env_idx = lua_gettop(_d->L);
                    lua_newtable(_d->L);
                    lua_pushglobaltable(_d->L);
                    lua_setfield(_d->L, -2, "__index");
                    lua_setmetatable(_d->L, env_idx);

                    // expose entity id
                    lua_pushinteger(_d->L, (lua_Integer)id);
                    lua_setfield(_d->L, env_idx, "eid");

                    // register a getter function per component present on this entity
                    for (auto&& curr : reg.storage())
                    {
                        auto& storage = curr.second;
                        if (!storage.contains(id))
                            continue;

                        entt::id_type comp_id = curr.first;
                        auto comp_type = entt::resolve(comp_id);
                        if (comp_type.info() == entt::type_id<void>())
                        {
                            log::verb("[script_lua] skipping unregistered storage: %x", comp_id);
                            continue;
                        }
                        const rtti::type_info *comp_info = comp_type.custom();
                        if (!comp_info)
                        {
                            log::verb("[script_lua] skipping component with no rtti info: %x", comp_id);
                            continue;
                        }

                        log::info("[script_lua] registering component getter c_%s for entity %x", (const char*)comp_info->identifier, id);

                        lua_pushinteger(_d->L, (lua_Integer)id);
                        lua_pushinteger(_d->L, (lua_Integer)comp_id);
                        lua_pushcclosure(_d->L, [](lua_State *L) -> int {
                            auto eid    = (entt::entity)  lua_tointeger(L, lua_upvalueindex(1));
                            auto cid    = (entt::id_type) lua_tointeger(L, lua_upvalueindex(2));
                            auto *stor  = engine::instance().default_scene().registry().storage(cid);
                            if (!stor || !stor->contains(eid)) { lua_pushnil(L); return 1; }
                            auto ctype  = entt::resolve(cid);
                            auto any    = ctype.from_void(stor->value(eid));
                            if (!any) { lua_pushnil(L); return 1; }
                            lua::push_meta_any(L, std::move(any));
                            return 1;
                        }, 2);
                        std::string getter_name = "c_";
                        getter_name += comp_info->identifier;
                        lua_setfield(_d->L, env_idx, getter_name.c_str());
                    }

                    // store the env table ref on the component so other code can retrieve it
                    lua_pushvalue(_d->L, env_idx);
                    script.env_ref = luaL_ref(_d->L, LUA_REGISTRYINDEX);

                    // register script_on_destroy(fn) — per-entity cleanup hook
                    {
                        lua_pushlightuserdata(_d->L, this);
                        lua_pushinteger(_d->L, (lua_Integer)entt::to_integral(id));
                        lua_pushcclosure(_d->L, [](lua_State *L) -> int {
                            if (!lua_isfunction(L, 1))
                                return luaL_error(L, "script_on_destroy: expected a function argument");
                            auto  *sys_raw = static_cast<script_lua*>(lua_touserdata(L, lua_upvalueindex(1)));
                            auto   eid     = static_cast<entt::entity>(lua_tointeger(L, lua_upvalueindex(2)));
                            lua_pushvalue(L, 1);
                            int ref = luaL_ref(L, LUA_REGISTRYINDEX);
                            sys_raw->_d->on_destroy_callbacks[eid].push_back(ref);
                            return 0;
                        }, 2);
                        lua_setfield(_d->L, env_idx, "script_on_destroy");
                    }

                    // attach environment to function's _ENV (upvalue 1)
                    lua_pushvalue(_d->L, env_idx);
                    lua_setupvalue(_d->L, func_idx, 1);
                    lua_pop(_d->L, 1);  // pop env

                    // call the chunk
                    int call_ret = lua_pcall(_d->L, 0, 0, 0);
                    if (call_ret == LUA_OK)
                    {
                        log::info("[script_lua] script ok: %x", id);
                    }
                    else
                    {
                        const char *err = lua_tostring(_d->L, -1);
                        log::error("[script_lua] script failed: %x: %s", id, err ? err : "(no message)");
                        lua_pop(_d->L, 1);
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

        // flush entities queued for destruction by scripts this frame
        engine::instance().default_scene().flush_destroy_queue();
    }
    return true;
}

bool script_lua::event(SDL_Event*)
{
    return true;
}

void script_lua::on_scene_change()
{
    // Disconnect signal from old registry before it's cleared.
    // EnTT fires on_destroy signals during registry::clear(), so callbacks will
    // already have been called for any remaining cscript entities. We just need
    // to clean up our map and reconnect to the new scene's registry afterward.
    _d->on_destroy_callbacks.clear();

    // Reconnect to the new scene's registry.
    engine::instance().default_scene().registry().on_destroy<cscript>().connect<&script_lua::_on_cscript_destroy>();
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
    lua::register_lua_function_type();
}
