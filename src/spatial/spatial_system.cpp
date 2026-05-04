#include <newbase/spatial/spatial_system.hpp>
#include <newbase/components/structure.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/log.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

using namespace nb;
using entt::operator""_hs;

spatial_system::spatial_system()  { log::info("[spatial_system] constructing"); }
spatial_system::~spatial_system() { log::info("[spatial_system] destroying");   }

// Recursive top-down transform propagation
static void propagate(entt::registry &reg, entt::entity e, const glm::mat4 &parent_world)
{
    auto *sp = reg.try_get<cspatial>(e);
    glm::mat4 world = parent_world;
    if (sp) {
        sp->apply(parent_world);
        world = sp->world;
    }
    auto *s = reg.try_get<cstructure>(e);
    if (!s) return;
    entt::entity child = s->first_child;
    while (child != entt::null && reg.valid(child)) {
        entt::entity next = reg.get<cstructure>(child).next_sibling;
        propagate(reg, child, world);
        child = next;
    }
}

void spatial_system::on_structure_destroy(entt::registry &reg, entt::entity e)
{
    auto &s = reg.get<cstructure>(e);

    // Unlink from parent's child list
    if (s.parent != entt::null && reg.valid(s.parent)) {
        if (auto *ps = reg.try_get<cstructure>(s.parent)) {
            if (ps->first_child == e) {
                ps->first_child = s.next_sibling;
            } else {
                entt::entity cur = ps->first_child;
                while (cur != entt::null && reg.valid(cur)) {
                    auto &cs = reg.get<cstructure>(cur);
                    if (cs.next_sibling == e) { cs.next_sibling = s.next_sibling; break; }
                    cur = cs.next_sibling;
                }
            }
        }
    }

    // Queue all children for destruction (they'll cascade via their own on_destroy)
    entt::entity child = s.first_child;
    while (child != entt::null && reg.valid(child)) {
        entt::entity next = reg.get<cstructure>(child).next_sibling;
        engine::instance().default_scene().queue_destroy(child);
        child = next;
    }
}

void spatial_system::connect_signals()
{
    auto &reg = engine::instance().default_scene().registry();
    m_on_destroy_conn = reg.on_destroy<cstructure>().connect<&spatial_system::on_structure_destroy>();
    m_needs_reconnect = false;
    log::info("[spatial_system] connected cstructure on_destroy signal");
}

bool spatial_system::init(ryml::ConstNodeRef)
{
    log::info("[spatial_system] init");
    cstructure::_ensure_rtti();
    connect_signals();
    return true;
}

void spatial_system::on_scene_change()
{
    m_needs_reconnect = true;
}

bool spatial_system::step(step_phase phase)
{
    if (phase == step_phase::PREPARE && m_needs_reconnect)
        connect_signals();

    if (phase == step_phase::POST_UPDATE) {
        auto &reg = engine::instance().default_scene().registry();

        // Propagate hierarchy roots (entities with cstructure and no parent)
        auto struct_view = reg.view<cstructure>();
        for (auto [e, s] : struct_view.each()) {
            if (s.parent == entt::null)
                propagate(reg, e, glm::mat4{1.0f});
        }

        // Apply standalone spatials (no cstructure) — ensures world matrix is current
        // even for entities whose scripts skip calling sp:apply()
        auto sp_view = reg.view<cspatial>(entt::exclude_t<cstructure>{});
        for (auto [e, sp] : sp_view.each())
            sp.apply();
    }

    return true;
}

// RTTI
extern "C" void _rtti_init_spatial_system()
{
    cstructure::_ensure_rtti();

    entt::meta_factory<nb::spatial_system>{}
        .type("spatial_system"_hs)
        .custom<rtti::type_info>(rtti::type_info{"spatial_system", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::spatial_system>>{rtti::ctx_systems()}
        .type("spatial_system_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::spatial_system>>()
        .conv<std::shared_ptr<nb::system>>();
}
