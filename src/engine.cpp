#include <newbase/engine.h>
#include <newbase/system.h>
#include <newbase/res/manager.h>
#include <newbase/scene.h>
#include <newbase/sdl/logging_handler.h>
#include <newbase/nb_config.h>
#include <newbase/log.h>

#include <SDL3/SDL.h>
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
    ryml::Tree cfg;
    std::string cfile;
    SDL_InitFlags initflags;
    int log_handler_handle;
    scene default_scene;
};

engine::engine()
{
    log::info("[engine] constructing");

    _d = new engine_p();
    _d->initflags = 0;

    log::setup_handler();
    _d->log_handler_handle = log::register_observer(std::bind(&engine::log_handler, this, 
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    log::info("[engine] logging ready");

    std::string cfgpath = std::string(NEWBASE_DEFAULT_RES_PREFIX) + "/config.yaml";
    std::ifstream t(cfgpath);
    if(!t.is_open())
    {
        log::critical("[engine] could not find config file: %s", cfgpath.c_str());
        exit(1);
    }
    std::stringstream buffer;
    buffer << t.rdbuf();
    _d->cfile = buffer.str();
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
            _d->initflags |= sys->sdl_subsystems();
            _systems.emplace_back(sys);
        }
    }

    log::info("[engine] constructed");
}

engine::~engine()
{
    log::info("[engine] destroying");

    // ensure there are not more system references from the engine
    _systems.clear();

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

    for(auto s: _systems)
    {
        if(!s->init(argc, argv))
            return false;
    }

    // load initial entity tree
    auto root_ent = _d->default_scene.build_etree("@res/root.et.yaml"_hs);
    

    log::info("[engine] initialized");
    return true;
}

bool engine::teardown()
{
    // destroy all system shared_ptrs
    _systems.clear();
    return true;
}

bool engine::step()
{
    log::debug("[engine] step");\

    for(int i = 0; i < nb::step_phase::_STEP_PHASE_COUNT; i++)
    {
        for(auto s: _systems)
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
        if(evt->key.key == SDLK_ESCAPE)
            return false;
    
    if(evt->type == SDL_EVENT_QUIT)
        return false;

    for(auto s: _systems)
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