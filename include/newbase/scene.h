#pragma once

#include <entt/fwd.hpp>
#include <entt/entity/entity.hpp>

namespace nb {

class scene final {
public:
    explicit scene();
    scene(const scene&) = delete;
    scene(scene&&) = delete;
    ~scene();

    entt::registry &registry();

    entt::id_type build_etree(entt::id_type retree_id, entt::id_type parent = entt::null);
};

}