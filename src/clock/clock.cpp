#include <newbase/clock/clock.hpp>
#include <newbase/log.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <entt/entt.hpp>
#include <SDL3/SDL.h>

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
    entt::locator<nb::clock*>::emplace(this);
    return true;
}

bool clock::step(step_phase phase)
{
    if(phase == step_phase::PREPARE)
    {
        const uint64_t now_ns = SDL_GetTicksNS();
        if (m_last_ns == 0) m_last_ns = now_ns;
        const float raw_dt = static_cast<float>(now_ns - m_last_ns) * 1e-9f;
        m_last_ns  = now_ns;
        m_real_dt  = (raw_dt < 0.1f) ? raw_dt : 0.1f;  // cap at 100ms
    }
    else if(phase == step_phase::GENERAL_UPDATE)
    {
        const float scaled_dt = m_real_dt * m_time_scale;
        m_iterating = true;
        for(auto& it: m_update)
            it.second(scaled_dt);
        for(auto& it: m_update_monotonic)
            it.second(m_real_dt);
        m_iterating = false;
        for(int h : m_pending_erase) { m_update.erase(h); m_update_monotonic.erase(h); }
        m_pending_erase.clear();
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
        .custom<rtti::type_info>(rtti::type_info{.identifier="clock", .type_class=rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>()
        .func<&nb::clock::update_add>("update_add"_hs)
        .custom<rtti::func_info>(rtti::func_info{.identifier="update_add"})
        .func<&nb::clock::update_remove>("update_remove"_hs)
        .custom<rtti::func_info>(rtti::func_info{.identifier="update_remove"})
        .func<&nb::clock::update_add_monotonic>("update_add_monotonic"_hs)
        .custom<rtti::func_info>(rtti::func_info{.identifier="update_add_monotonic"})
        .func<&nb::clock::set_time_scale>("set_time_scale"_hs)
        .custom<rtti::func_info>(rtti::func_info{.identifier="set_time_scale"})
        .func<&nb::clock::get_time_scale>("get_time_scale"_hs)
        .custom<rtti::func_info>(rtti::func_info{.identifier="get_time_scale"});
    entt::meta_factory<std::shared_ptr<nb::clock>>{rtti::ctx_systems()}
        .type("clock_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::clock>>()
        .conv<std::shared_ptr<nb::system>>();
}