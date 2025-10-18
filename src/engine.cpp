#include <newbase/engine.hpp>
#include <newbase/system.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/scene.hpp>
#include <newbase/sdl/logging_handler.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/nb_config.h>
#include <newbase/log.hpp>
#ifdef NEWBASE_USE_XDG_DATA_DIRS
#include <newbase/utility/xdg.hpp>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_init.h>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <entt/entt.hpp>

#include <functional>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include <SDL3/SDL_stdinc.h>

using namespace nb;
using entt::operator""_hs;


struct nb::engine_p {
    std::vector<std::shared_ptr<system>> _systems;
    std::unordered_map<entt::id_type, std::shared_ptr<system>> _systems_meta;
    std::vector<ryml::ConstNodeRef> _sys_cfgs;

    ryml::Tree cfg;
    std::string cfile;
    SDL_InitFlags initflags;
    int log_handler_handle;
    scene default_scene;

    bool exit_requested {false};

    std::array<uint64_t, NB_FRAMECOUNTER_SAMPLES+1> fc_update_start;
    std::array<uint64_t, NB_FRAMECOUNTER_SAMPLES+1> fc_update_end;
    std::array<uint64_t, NB_FRAMECOUNTER_SAMPLES+1> fc_min_event_start;
    std::array<uint64_t, NB_FRAMECOUNTER_SAMPLES+1> fc_max_event_end;
    int framecounter_start;

    std::map<int, std::string> dbg_action_names;
    std::map<int, std::function<void(void)>> dbg_action_callbacks;
    int dbg_action_next_idx = 0;
};

engine::engine()
{
    log::info("[engine] constructing (thread id: %lu)", SDL_ThreadID());

    _d = new engine_p();
    _d->initflags = 0;

#ifndef ANDROID
    // leave android using default log handler
    log::setup_handler();
    _d->log_handler_handle = log::register_observer(std::bind(&engine::log_handler, this, 
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#endif

    log::info("[engine] logging ready");

    std::string cfgpath = std::string(NEWBASE_DEFAULT_RES_PREFIX) + "/config.yaml";
#ifdef NEWBASE_USE_XDG_DATA_DIRS
    if(_nb_xdg_data_dir_found())
    {
        cfgpath = _nb_xdg_data_dirname_get() + std::string{"/"} + cfgpath;
    }
#endif
    void *data = SDL_LoadFile(cfgpath.c_str(), nullptr);
    if(!data)
    {
        log::critical("[engine] could not open config file: %s", cfgpath.c_str());
        exit(1);
    }
    _d->cfile = std::string{reinterpret_cast<const char *>(data)};
    SDL_free(data);

    _d->cfg = ryml::parse_in_place(_d->cfile.data());

    // could be useful but no
    //log::info("[engine] config:\n%s",
    //            ryml::emitrs_yaml<std::string>(_d->cfg).c_str());

    auto resources = _d->cfg["resources"];
    if(!rman().configure(resources))
    {
        log::critical("[engine] could not initialize resource manager!");
        exit(1);
    }

    auto systems = _d->cfg["systems"];
    for(ryml::ConstNodeRef n : systems.children())
    {
        std::string sysname;
        c4::from_chars(n.key(), &sysname);
        log::info("[engine] creating system: %s", sysname.c_str());
        std::shared_ptr<system> sys = system::build(sysname.c_str(), n.tree());
        if(!sys)
        {
            log::error("[engine] could not create system: %s", sysname.c_str());
        }
        else
        {
            _d->initflags |= sys->sdl_subsystems(n);
            _d->_systems.emplace_back(sys);
            _d->_systems_meta[sys->metatype_id()] = sys;
            _d->_sys_cfgs.push_back(n);
        }
    }

    log::info("[engine] constructed");
}

engine::~engine()
{
    log::info("[engine] destroying");

    // ensure there are not more system references from the engine
    _d->_systems.clear();
    _d->_systems_meta.clear();

    log::unregister_observer(_d->log_handler_handle);
    delete _d;

    log::info("[engine] destroyed");
}

bool engine::init(int argc, char ** argv)
{
    log::info("[engine] init");

    // TODO collect SDL systems to init from systems
    if(!SDL_Init(_d->initflags))
    {
        log::error("[engine] SDL_Init error: (%d): %s", static_cast<int>(_d->initflags), SDL_GetError());
        return false;
    }

    auto cfg_it = _d->_sys_cfgs.begin();
    for(auto s: _d->_systems)
    {
        if(!s->init(*cfg_it++))
            return false;
    }

    // load initial entity tree
    auto root_ent = _d->default_scene.build_etree("res/root.et.yaml"_hs);
    log::info("[engine] root tree: %x", root_ent);

    log::info("[engine] initialized");
    return true;
}

bool engine::teardown()
{
    // destroy all system shared_ptrs
    _d->_systems.clear();
    _d->_systems_meta.clear();
    return true;
}

bool engine::step()
{
    log::debug("[engine] step");

    if(_d->exit_requested)
    {
        log::info("[engine] exiting");
        return false;
    }

    for(int i = 0; i < nb::step_phase::_STEP_PHASE_COUNT; i++)
    {
        for(auto s: _d->_systems)
        {
        // TODO check if system uses this phase (use some sort of mask?)
            if(!s->step(static_cast<nb::step_phase>(i)))
                return false;
        }
    }

    return true;
}

bool engine::event(SDL_Event *evt)
{
    log::debug("[engine] event");
    if(evt->type == SDL_EVENT_KEY_DOWN)
    {
        if(evt->key.key == SDLK_ESCAPE)
            return false;
        else if((evt->key.key >= SDLK_0 && evt->key.key <= SDLK_9) || evt->key.scancode == SDL_SCANCODE_GRAVE)
        {
            int idx = evt->key.key - SDLK_0;
            if(evt->key.scancode == SDL_SCANCODE_GRAVE)
                idx = 0;

            auto it = _d->dbg_action_callbacks.find(idx);
            if(it != _d->dbg_action_callbacks.end())
            {
                log::info("[engine] debug action triggered: (%d) '%s'", it->first, _d->dbg_action_names[idx].c_str());
                it->second();
            }
        }
    }
    
    if(evt->type == SDL_EVENT_QUIT)
        return false;

    for(auto s: _d->_systems)
    {
        // TODO check if system takes event (use some sort of mask?)
        if(!s->event(evt))
            return false;
    }

    return true;
}

::nb::scene& engine::default_scene()
{
    return _d->default_scene;
}


void engine::request_exit()
{
    log::info("[engine] exit requested");
    _d->exit_requested = true;
}


std::shared_ptr<::nb::system> engine::system_from_id(entt::id_type meta_id)
{
    auto it = _d->_systems_meta.find(meta_id);
    return it != _d->_systems_meta.end()? it->second : nullptr;
}

int engine::debug_action_register(std::string name, std::function<void(void)> callback,  int idx)
{
    if(idx == -1)
    {
        idx = _d->dbg_action_next_idx++;
    }

    _d->dbg_action_names[idx] = name;
    _d->dbg_action_callbacks[idx] = callback;
    return idx;
}

bool engine::debug_action_unregister(int index)
{
    if(_d->dbg_action_names.find(index) != _d->dbg_action_names.end())
    {
        _d->dbg_action_names.erase(index);
        _d->dbg_action_callbacks.erase(index);
        return true;
    }
    return false;
}

const std::map<int, std::string>& engine::debug_action_names()
{
    return _d->dbg_action_names;
}

void engine::log_handler(int category, int prio, const char *msg)
{
    static bool checked_color = false;
    static bool use_color = false;
    if(!checked_color)
    {
        // maybe move check to ::nb::log?
        const char * term = SDL_getenv("TERM");
        if(term && strstr(term, "xterm") == term)
            use_color = true;
        checked_color = true;
    }
    auto ansi = ::nb::log::priority_ansi_decor(static_cast<::nb::log::priority>(prio));
    std::cout << (ansi.first? ansi.first : "") <<"["<< ::nb::log::priority_str(static_cast<::nb::log::priority>(prio)) <<
         "] [" << ::nb::log::category_str(static_cast<::nb::log::category>(category)) << "] "<< msg << (ansi.second? ansi.second : "") << std::endl;
}

engine& engine::instance()
{
    static engine e;
    return e;
}

extern "C" void _rtti_init_engine()
{
    /*  does not work like this because type needs to be movable
        for now bindings are done by hand :(
        same for scene

    entt::meta_factory<nb::engine>{}
        .type("engine"_hs)
        .custom<rtti::singleton_info>("engine")
        .func<&nb::engine::request_exit>("request_exit"_hs)
        .custom<rtti::func_info>("request_exit");
    */
}