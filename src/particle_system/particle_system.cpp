#include <newbase/particle_system/particle_system.hpp>
#include <newbase/components/particle_emitter.hpp>
#include <newbase/components/mesh2d.hpp>
#include <newbase/clock/clock.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/log.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/contexts.hpp>
#include <entt/entt.hpp>
#include <entt/meta/factory.hpp>
#include <glm/trigonometric.hpp>
#include <glm/common.hpp>
#include <cmath>
#include <cstdint>

using namespace nb;
using entt::operator""_hs;

// ---------------------------------------------------------------------------
// RNG — xorshift64, fits in a register, good enough for visual randomness
// ---------------------------------------------------------------------------

static uint64_t s_rng = 0x853c49e6748fea9bULL;

static float rnd()
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 7;
    s_rng ^= s_rng << 17;
    // map [0, 2^31) to [-1, 1)
    return static_cast<float>(s_rng >> 33) / static_cast<float>(0x40000000u) - 1.f;
}

// ---------------------------------------------------------------------------
// Pool / simulation helpers
// ---------------------------------------------------------------------------

static void _ensure_pool(cparticle_emitter& e)
{
    if (!e.res) return;
    const int cap = e.res->max_particles;
    if (e.pool.capacity != cap)
    {
        e.pool.allocate(cap);
        e.leftover = 0.f;
    }
    if (!e.geom)
        e.geom = std::make_shared<geometry_buffer_2d>();
}

static void _emit_one(cparticle_emitter& e)
{
    const int idx = e.pool.find_dead();
    if (idx < 0) return;

    const auto& cfg = e.res->emitter;

    e.pool.pos[idx] = { cfg.pos_variance.x * rnd(), cfg.pos_variance.y * rnd() };

    glm::vec2 vel = cfg.vel + glm::vec2(cfg.vel_variance.x * rnd(), cfg.vel_variance.y * rnd());
    if (cfg.vel_angle_variance != 0.f)
    {
        const float angle = glm::radians(cfg.vel_angle_variance * rnd());
        const float c = std::cos(angle), s = std::sin(angle);
        vel = { vel.x * c - vel.y * s, vel.x * s + vel.y * c };
    }
    e.pool.vel[idx] = vel;

    e.pool.color[idx] = glm::clamp(
        cfg.color + cfg.color_variance * glm::vec4(rnd(), rnd(), rnd(), rnd()),
        glm::vec4(0.f), glm::vec4(1.f));

    e.pool.scale[idx]    = std::max(0.f, cfg.scale + cfg.scale_variance * rnd());
    e.pool.rotation[idx] = glm::radians(cfg.rotation + cfg.rotation_variance * rnd());

    const float lt       = std::max(0.001f, cfg.lifetime + cfg.lifetime_variance * rnd());
    e.pool.ttl[idx]      = lt;
    e.pool.ttl_max[idx]  = lt;
}

static void _apply_affectors(cparticle_emitter& e, float dt)
{
    auto& p = e.pool;
    for (const auto& aff : e.res->affectors)
    {
        using AT = particle_affector_config::type;
        switch (aff.affector_type)
        {
        case AT::ACCELERATION:
            for (int i = 0; i < p.capacity; ++i)
                if (p.ttl[i] > 0.f)
                    p.vel[i] += aff.vec2_val * dt;
            break;

        case AT::DRAG:
            for (int i = 0; i < p.capacity; ++i)
                if (p.ttl[i] > 0.f)
                    p.vel[i] -= p.vel[i] * aff.vec2_val * dt;
            break;

        case AT::COLOR_FADE:
            for (int i = 0; i < p.capacity; ++i)
            {
                if (p.ttl[i] <= 0.f) continue;
                p.color[i] = glm::clamp(p.color[i] + aff.vec4_val * dt, glm::vec4(0.f), glm::vec4(1.f));
                if (aff.kill_on_zero && p.color[i].a <= 0.f)
                    p.ttl[i] = 0.f;
            }
            break;

        case AT::SCALE_FADE:
            for (int i = 0; i < p.capacity; ++i)
            {
                if (p.ttl[i] <= 0.f) continue;
                p.scale[i] += aff.float_val * dt;
                if (p.scale[i] < 0.f)
                {
                    p.scale[i] = 0.f;
                    if (aff.kill_on_zero) p.ttl[i] = 0.f;
                }
            }
            break;

        case AT::ATTRACTOR:
            for (int i = 0; i < p.capacity; ++i)
            {
                if (p.ttl[i] <= 0.f) continue;
                p.vel[i] += (aff.vec2_val - p.pos[i]) * aff.float_val * dt;
            }
            break;
        }
    }
}

static void _integrate(cparticle_emitter& e, float dt)
{
    auto& p = e.pool;
    for (int i = 0; i < p.capacity; ++i)
    {
        if (p.ttl[i] <= 0.f) continue;
        p.ttl[i] -= dt;
        p.pos[i] += p.vel[i] * dt;
    }
}

static void _emit_batch(cparticle_emitter& e, float dt)
{
    if (!e.emitting) return;
    const float rate    = e.emit_rate_override >= 0.f ? e.emit_rate_override : e.res->emitter.rate;
    const float to_emit = e.leftover + rate * dt;
    const int   n       = static_cast<int>(to_emit);
    e.leftover          = to_emit - static_cast<float>(n);
    for (int i = 0; i < n; ++i)
        _emit_one(e);
}

static void _rebuild_geometry(cparticle_emitter& e)
{
    e.geom->clear();
    auto& p = e.pool;
    for (int i = 0; i < p.capacity; ++i)
    {
        if (p.ttl[i] <= 0.f) continue;
        const float hs  = p.scale[i] * 0.5f;
        const float cr  = std::cos(p.rotation[i]);
        const float sr  = std::sin(p.rotation[i]);
        const auto& pos = p.pos[i];
        const auto& col = p.color[i];

        auto corner = [&](float lx, float ly) -> geometry_buffer_2d::vertex {
            return {
                .pos   = { pos.x + lx * cr - ly * sr, pos.y + lx * sr + ly * cr },
                .uv    = { lx > 0.f ? 1.f : 0.f, ly > 0.f ? 1.f : 0.f },
                .color = col
            };
        };

        e.geom->push_quad(
            corner(-hs, -hs), corner( hs, -hs),
            corner(-hs,  hs), corner( hs,  hs));
    }
}

// ---------------------------------------------------------------------------
// particle_system
// ---------------------------------------------------------------------------

particle_system::particle_system()  = default;
particle_system::~particle_system() = default;

bool particle_system::init(ryml::ConstNodeRef /*cfg*/) { return true; }

bool particle_system::step(step_phase phase)
{
    if (phase != step_phase::GENERAL_UPDATE) return true;

    const float dt = entt::locator<nb::clock*>::has_value()
        ? entt::locator<nb::clock*>::value()->get_dt() : (1.0f/60.0f);

    auto& reg  = engine::instance().default_scene().registry();
    auto  view = reg.view<cparticle_emitter>();

    for (auto [eid, emitter] : view.each())
    {
        if (!emitter.res) continue;
        _ensure_pool(emitter);

        if (!emitter.pre_simulated)
        {
            emitter.pre_simulated = true;
            for (int i = 0; i < emitter.res->initial_burst; ++i)
                _emit_one(emitter);
            constexpr float pre_dt = 1.f / 20.f;
            const int frames = emitter.res->pre_simulate_frames;
            for (int f = 0; f < frames; ++f)
            {
                _apply_affectors(emitter, pre_dt);
                _integrate(emitter, pre_dt);
                _emit_batch(emitter, pre_dt);
            }
        }

        _apply_affectors(emitter, dt);
        _integrate(emitter, dt);
        _emit_batch(emitter, dt);
        _rebuild_geometry(emitter);

        // Keep sibling cmesh2d in sync; auto-create if missing
        auto* mesh = reg.try_get<cmesh2d>(eid);
        if (!mesh) mesh = &reg.emplace<cmesh2d>(eid);
        mesh->geom       = emitter.geom;
        mesh->tex        = emitter.res->tex;
        mesh->blend_mode = (emitter.res->blend_mode == particle_blend_mode::ADD)
                           ? blend_mode_2d::ADD : blend_mode_2d::ALPHA;
    }

    return true;
}

void particle_system::burst(entt::entity ent, int count)
{
    auto& reg     = engine::instance().default_scene().registry();
    auto* emitter = reg.try_get<cparticle_emitter>(ent);
    if (!emitter || !emitter->res) return;
    _ensure_pool(*emitter);
    for (int i = 0; i < count; ++i)
        _emit_one(*emitter);
}

// ---------------------------------------------------------------------------
// RTTI
// ---------------------------------------------------------------------------

extern "C" void _rtti_init_particle_system()
{
    entt::meta_factory<nb::particle_system>{}
        .type("particle_system"_hs)
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "particle_system",
            .type_class = rtti::TYPE_CLASS_SYSTEM
        })
        .base<nb::system>()
        .func<&nb::particle_system::burst>("burst"_hs)
            .custom<rtti::func_info>(rtti::func_info{"burst"});
    entt::meta_factory<std::shared_ptr<nb::particle_system>>{rtti::ctx_systems()}
        .type("particle_system_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::particle_system>>()
        .conv<std::shared_ptr<nb::system>>();
}
