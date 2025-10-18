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

    entt::id_type build_etree(entt::id_type retree_id, entt::id_type parent = entt::null);
private:
    scene_p *_d;
};

}