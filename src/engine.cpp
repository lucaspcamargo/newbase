#include <newbase/engine.h>
#include <newbase/system.h>
#include <newbase/res/manager.h>
#include <newbase/ecs.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <entt/entt.hpp>

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
};

engine::engine()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] constructing");

    _d = new engine_p();
    _d->initflags = 0;

    std::ifstream t("config.yaml");
    if(!t.is_open())
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "[engine] could not find config file!");
        exit(1);
    }
    std::stringstream buffer;
    buffer << t.rdbuf();
    _d->cfile = buffer.str();
    _d->cfg = ryml::parse_in_place(_d->cfile.data());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[engine] config:\n%s",
                ryml::emitrs_yaml<std::string>(_d->cfg).c_str());

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
