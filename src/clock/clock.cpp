#include <newbase/clock/clock.h>
#include <newbase/log.h>
#include <newbase/reflection/contexts.h>
#include <newbase/reflection/data.h>
#include <entt/entt.hpp>

using namespace nb;
using entt::operator""_hs;

clock::clock()
{
    log::info("[clock] constructing");
}

clock::~clock()
{
    log::info("[clock] destroying");
    log::info("[clock] destroyed");
}

bool clock::init(ryml::ConstNodeRef cfg)
{
    log::info("[clock] init");
    return true;
}

bool clock::step(step_phase phase)
{
    if(phase == step_phase::GENERAL_UPDATE)
    {
        // fire update callbacks
        for(auto it: m_update)
            it.second(1.0f/60.0f);
    }

    return true;
}

bool clock::event(SDL_Event*)
{
    // not interested in SDL events
    return true;
}


// RTTI metadata
extern "C" void _rtti_init_clock()
{
    entt::meta_factory<nb::clock>{}
        .type("clock"_hs)
        .custom<rtti::system_info>(rtti::system_info{"clock"})
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::clock>>{rtti::ctx_systems()}
        .type("clock_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::clock>>()
        .conv<std::shared_ptr<nb::system>>();
}