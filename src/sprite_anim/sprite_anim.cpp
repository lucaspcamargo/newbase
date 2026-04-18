#include <newbase/sprite_anim/sprite_anim.hpp>
#include <newbase/components/sprite.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/contexts.hpp>
#include <entt/entt.hpp>
#include <entt/meta/factory.hpp>
#include <SDL3/SDL.h>

using namespace nb;
using entt::operator""_hs;

sprite_anim::sprite_anim()  = default;
sprite_anim::~sprite_anim() = default;

bool sprite_anim::init(ryml::ConstNodeRef /*cfg*/) { return true; }

bool sprite_anim::step(step_phase phase)
{
    if (phase != step_phase::GENERAL_UPDATE) return true;

    static uint64_t last_ns = SDL_GetTicksNS();
    const uint64_t  now_ns  = SDL_GetTicksNS();
    const float     dt      = static_cast<float>(now_ns - last_ns) * 1e-9f;
    last_ns = now_ns;

    auto& reg  = engine::instance().default_scene().registry();
    auto  view = reg.view<csprite>();

    for (auto [eid, cs] : view.each())
    {
        if (!cs.spr) continue;
        const auto& seqs = cs.spr->sequences;
        if (seqs.empty()) continue;

        // Resolve current sequence pointer
        const sprite_sequence* seq = cs.sequence.empty()
            ? cs.spr->first_sequence()
            : cs.spr->find_sequence(cs.sequence);
        if (!seq || seq->frames.empty()) continue;

        // Clamp frame index in case sequence changed externally
        if (cs.frame >= static_cast<int>(seq->frames.size()))
            cs.frame = 0;

        if (cs.animating)
        {
            cs.time_in_frame += dt;

            while (cs.time_in_frame >= seq->frames[cs.frame].duration)
            {
                cs.time_in_frame -= seq->frames[cs.frame].duration;
                cs.frame++;

                if (cs.frame >= static_cast<int>(seq->frames.size()))
                {
                    if (!seq->next.empty())
                    {
                        const sprite_sequence* nxt = cs.spr->find_sequence(seq->next);
                        if (nxt && !nxt->frames.empty())
                        {
                            cs.sequence      = seq->next;
                            seq              = nxt;
                            cs.frame         = 0;
                            cs.time_in_frame = 0.f;
                            break;
                        }
                    }

                    if (seq->loop)
                        cs.frame = 0;
                    else
                    {
                        cs.frame     = static_cast<int>(seq->frames.size()) - 1;
                        cs.animating = false;
                        break;
                    }
                }
            }
        }

        // Unconditional cache check — catches changes from animation, scripts, or anything else
        const std::string& eff_seq = cs.sequence.empty() ? seq->name : cs.sequence;
        if (cs._cached_sequence != eff_seq || cs._cached_frame != cs.frame)
        {
            cs.current_source_rect = seq->frames[cs.frame].source_rect;
            cs._cached_sequence    = eff_seq;
            cs._cached_frame       = cs.frame;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// RTTI
// ---------------------------------------------------------------------------

extern "C" void _rtti_init_sprite_anim()
{
    entt::meta_factory<nb::sprite_anim>{}
        .type("sprite_anim"_hs)
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "sprite_anim",
            .type_class = rtti::TYPE_CLASS_SYSTEM
        })
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::sprite_anim>>{rtti::ctx_systems()}
        .type("sprite_anim_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::sprite_anim>>()
        .conv<std::shared_ptr<nb::system>>();
}
