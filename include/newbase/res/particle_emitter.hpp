#pragma once

#include <newbase/res/resource.hpp>
#include <newbase/res/texture.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <entt/entt.hpp>
#include <vector>
#include <memory>

namespace nb {

enum class particle_blend_mode { ALPHA, ADD };

struct particle_affector_config {
    enum class type { ACCELERATION, DRAG, COLOR_FADE, SCALE_FADE, ATTRACTOR };

    type      affector_type  { type::ACCELERATION };
    glm::vec2 vec2_val       { 0.f, 0.f };  // acceleration vector, drag coefficients, attractor position
    glm::vec4 vec4_val       { 0.f, 0.f, 0.f, 0.f }; // color_fade: rgba_per_sec
    float     float_val      { 0.f };        // scale_fade: per_sec; attractor: gain
    bool      kill_on_zero   { false };      // color_fade: kill when alpha=0; scale_fade: kill when scale=0
};

struct particle_emitter_config {
    float     rate               { 50.f };
    float     lifetime           { 1.f  };
    float     lifetime_variance  { 0.f  };
    glm::vec2 pos_variance       { 0.f, 0.f };
    glm::vec2 vel                { 0.f, 0.f };
    glm::vec2 vel_variance       { 0.f, 0.f };
    float     vel_angle_variance { 0.f };    // degrees — randomly rotates velocity vector
    glm::vec4 color              { 1.f, 1.f, 1.f, 1.f };
    glm::vec4 color_variance     { 0.f, 0.f, 0.f, 0.f };
    float     scale              { 10.f };
    float     scale_variance     { 0.f  };
    float     rotation           { 0.f  };   // degrees
    float     rotation_variance  { 0.f  };   // degrees
};

struct rparticle_emitter : public resource {
    explicit rparticle_emitter(entt::id_type id = 0)
        : resource(id, entt::hashed_string{"rparticle_emitter"}.value()) {}

    int                                   max_particles        { 200 };
    int                                   pre_simulate_frames  { 0 };
    int                                   initial_burst        { 0 };
    particle_blend_mode                   blend_mode           { particle_blend_mode::ALPHA };
    std::shared_ptr<rtexture>             tex;
    particle_emitter_config               emitter;
    std::vector<particle_affector_config> affectors;
};

} // namespace nb
