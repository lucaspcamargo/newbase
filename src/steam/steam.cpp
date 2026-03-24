#include <newbase/steam/steam.hpp>
#include <newbase/log.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <entt/entt.hpp>
#include <entt/meta/factory.hpp>

#include "steam/steam_api.h"

using namespace nb;
using entt::operator""_hs;


struct nb::steam_p {
    bool init_ok {false};
    bool steam_running {false};
};

steam::steam() : _d(std::make_unique<steam_p>()) 
{
    log::info("[steam] constructing");
}

steam::~steam() 
{
    log::info("[steam] destroyed");
};

bool steam::init(ryml::ConstNodeRef cfg) {
    // Stub for initialization
    _d->init_ok = SteamAPI_Init();
    log::info("[steam] init %s", _d->init_ok? "OK" : "NOK");

    _d->steam_running = SteamAPI_IsSteamRunning();

    if(_d->init_ok)
    {

    }
    else
    {
        if(!SteamAPI_IsSteamRunning())
        {
            log::warn("[steam] Steam is not running");
        }
    }
    

    return true;
}

bool steam::step(step_phase phase) {
    // Stub for step logic
    return true;
}

bool steam::event(SDL_Event *ev) {
    // Stub for event handling
    return true;
}


// RTTI metadata
extern "C" void _rtti_init_steam()
{
    // main interface
    entt::meta_factory<nb::steam>{}
        .type("steam"_hs)
        .custom<rtti::type_info>(rtti::type_info{"steam", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>();

    // factory
    entt::meta_factory<std::shared_ptr<nb::steam>>{rtti::ctx_systems()}
        .type("steam_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::steam>>()
        .conv<std::shared_ptr<nb::system>>();
}