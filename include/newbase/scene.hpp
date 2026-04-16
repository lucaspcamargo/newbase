#pragma once

#include <newbase/mixins.hpp>
#include <entt/fwd.hpp>
#include <entt/entity/entity.hpp>

namespace nb {

struct scene_p;

class scene final : public nocopy {
public:
    explicit scene(entt::id_type scene_id = entt::null);
    virtual ~scene();

    entt::registry& registry();

    // Instantiate all entities defined in the etree resource. Returns the first
    // created entity, or entt::null if nothing was created.
    entt::entity build_etree(entt::id_type retree_id, entt::id_type parent = entt::null);

    // Queue an entity for destruction. The entity is destroyed at the next
    // flush_destroy_queue() call (end of PREPARE phase).
    void queue_destroy(entt::entity e);
    void flush_destroy_queue();
private:
    scene_p *_d;
};

}