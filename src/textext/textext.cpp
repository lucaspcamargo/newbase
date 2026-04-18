#include <newbase/textext/textext.hpp>
#include <newbase/components/textext.hpp>
#include <newbase/components/mesh2d.hpp>
#include <newbase/geom/geometry_buffer_2d.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/contexts.hpp>
#include <entt/entt.hpp>
#include <entt/meta/factory.hpp>

using namespace nb;
using entt::operator""_hs;

bool textext::init(ryml::ConstNodeRef /*cfg*/) { return true; }

bool textext::step(step_phase phase)
{
    if (phase != step_phase::PRE_RENDER) return true;

    auto& reg  = engine::instance().default_scene().registry();
    auto  view = reg.view<ctextext, cmesh2d>();

    for (auto [eid, ct, mesh] : view.each())
    {
        if (!ct.dirty) continue;
        if (!ct.font || !ct.font->atlas) continue;

        if (!mesh.geom)
            mesh.geom = std::make_shared<geometry_buffer_2d>();
        mesh.geom->clear();
        mesh.tex        = ct.font->atlas;
        mesh.blend_mode = blend_mode_2d::ALPHA;
        mesh.visible    = true;

        const auto& font = *ct.font;
        float cx = 0.f;
        float baseline_y = static_cast<float>(font.ascent);

        for (unsigned char ch : ct.text)
        {
            const rtexfont::glyph* g = font.get_glyph((int)ch);
            if (!g) { cx += font.ascent * 0.25f; continue; } // fallback advance

            if (g->bw > 0 && g->bh > 0)
            {
                float x0 = cx + g->bx;
                float y0 = baseline_y + g->by;
                float x1 = x0 + g->bw;
                float y1 = y0 + g->bh;

                geometry_buffer_2d::vertex tl { {x0, y0}, {g->u0, g->v0}, ct.color };
                geometry_buffer_2d::vertex tr { {x1, y0}, {g->u1, g->v0}, ct.color };
                geometry_buffer_2d::vertex bl { {x0, y1}, {g->u0, g->v1}, ct.color };
                geometry_buffer_2d::vertex br { {x1, y1}, {g->u1, g->v1}, ct.color };
                mesh.geom->push_quad(tl, tr, bl, br);
            }

            cx += static_cast<float>(g->advance);
        }

        ct.dirty = false;
    }

    return true;
}

void textext::set_text(entt::entity ent, std::string text)
{
    auto& reg = engine::instance().default_scene().registry();
    if (auto* ct = reg.try_get<ctextext>(ent))
    {
        ct->text  = std::move(text);
        ct->dirty = true;
    }
}

// ---------------------------------------------------------------------------
// RTTI
// ---------------------------------------------------------------------------

extern "C" void _rtti_init_textext()
{
    ctextext::_ensure_rtti();

    entt::meta_factory<nb::textext>{}
        .type("textext"_hs)
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "textext",
            .type_class = rtti::TYPE_CLASS_SYSTEM
        })
        .base<nb::system>()
        .func<&nb::textext::set_text>("set_text"_hs)
        .custom<rtti::func_info>(rtti::func_info{"set_text"});
    entt::meta_factory<std::shared_ptr<nb::textext>>{rtti::ctx_systems()}
        .type("textext_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::textext>>()
        .conv<std::shared_ptr<nb::system>>();
}
