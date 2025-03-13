#include <newbase/engine.h>
#include <newbase/system.h>
#include <newbase/res/manager.h>
#include <newbase/ecs.h>
#include <newbase/sdl/logging_handler.h>
#include <newbase/nb_config.h>

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

using namespace nb;
using entt::operator""_hs;

struct nb::engine_p {
    ryml::Tree cfg;
    std::string cfile;
    SDL_InitFlags initflags;
    int log_handler_handle;
};

engine::engine()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] constructing");

    _d = new engine_p();
    _d->initflags = 0;

    log::setup_handler();
    _d->log_handler_handle = log::register_observer(std::bind(&engine::log_handler, this, 
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "[engine] base logging ready");
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] base logging ready");
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[engine] base logging ready");
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[engine] base logging ready");
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "[engine] base logging ready");

    std::string cfgpath = std::string(NEWBASE_DEFAULT_RES_PREFIX) + "/config.yaml";
    std::ifstream t(cfgpath);
    if(!t.is_open())
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "[engine] could not find config file: %s", cfgpath.c_str());
        exit(1);
    }
    std::stringstream buffer;
    buffer << t.rdbuf();
    _d->cfile = buffer.str();
    _d->cfg = ryml::parse_in_place(_d->cfile.data());

    // could be useful but no
    //SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] config:\n%s",
    //            ryml::emitrs_yaml<std::string>(_d->cfg).c_str());

    auto resources = _d->cfg["resources"];
    if(!rman().configure(resources))
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "[engine] could not initialize resource manager!");
        exit(1);
    }

    auto systems = _d->cfg["systems"];
    for(ryml::ConstNodeRef n : systems.children())
    {
        std::string sysname;
        c4::from_chars(n.key(), &sysname);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] creating system: %s", sysname.c_str());
        std::shared_ptr<system> sys = system::build(sysname.c_str(), n.tree());
        if(!sys)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[engine] could not create system: %s", sysname.c_str());
        }
        else
        {
            _d->initflags |= sys->sdl_subsystems();
            _systems.emplace_back(sys);
        }
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] constructed");
}

engine::~engine()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] destroying");

    // destroy all system shared_ptrs
    _systems.clear();

    log::unregister_observer(_d->log_handler_handle);

    delete _d;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] destroyed");
}

bool engine::init(int argc, char ** argv)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] init");

    // TODO collect SDL systems to init from systems
    if(!SDL_Init(_d->initflags))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[engine] SDL_Init error: (%d): %s", static_cast<int>(_d->initflags), SDL_GetError());
        return false;
    }

    for(auto s: _systems)
    {
        if(!s->init(argc, argv))
            return false;
    }

    // load initial entity tree
    auto root_ent = build_etree("@res/root.et.yaml"_hs);
    

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] initialized");
    return true;
}

bool engine::step()
{
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "[engine] step");\

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
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "[engine] event");
    if(evt->type == SDL_EVENT_KEY_DOWN)
        if(evt->key.key == SDLK_ESCAPE)
            return false;

    for(auto s: _systems)
    {
        // TODO check if system takes event (use some sort of mask?)
        if(!s->event(evt))
            return false;
    }

    return true;
}

void engine::log_handler(int category, int prio, const char *msg)
{
    auto ansi = ::nb::log::priority_ansi_decor(static_cast<::nb::log::priority>(prio));
    std::cout << (ansi.first? ansi.first : "") <<"["<< ::nb::log::priority_str(static_cast<::nb::log::priority>(prio)) <<
         "] [" << ::nb::log::category_str(static_cast<::nb::log::category>(category)) << "] "<< msg << (ansi.second? ansi.second : "") << std::endl;
}