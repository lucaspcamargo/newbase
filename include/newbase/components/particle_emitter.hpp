#pragma once

#include <newbase/res/particle_emitter.hpp>
#include <newbase/geom/geometry_buffer_2d.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <vector>
#include <memory>

namespace nb {

struct particle_pool {
    std::vector<glm::vec2> pos;
    std::vector<glm::vec2> vel;
    std::vector<glm::vec4> color;
    std::vector<float>     scale;
    std::vector<float>     rotation; // radians
    std::vector<float>     ttl;      // <= 0 means dead
    std::vector<float>     ttl_max;  // initial ttl (for normalized age)
    int                    capacity  { 0 };

    void allocate(int n)
    {
        capacity = n;
        pos.assign(n, {}); vel.assign(n, {});
        color.assign(n, {}); scale.assign(n, 0.f);
        rotation.assign(n, 0.f); ttl.assign(n, 0.f); ttl_max.assign(n, 1.f);
    }

    int find_dead() const
    {
        for (int i = 0; i < capacity; ++i)
            if (ttl[i] <= 0.f) return i;
        return -1;
    }
};

struct cparticle_emitter {
    std::shared_ptr<rparticle_emitter>  res;
    bool                                emitting           { true  };
    bool                                pixel_snap         { false };
    float                               emit_rate_override { -1.f  }; // < 0 = use resource rate

    // Runtime state — managed by particle_system
    particle_pool                       pool;
    float                               leftover       { 0.f  };
    bool                                pre_simulated  { false };
    std::shared_ptr<geometry_buffer_2d> geom;

    static void _ensure_rtti();
};

} // namespace nb
