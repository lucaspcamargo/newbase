#pragma once

#include <entt/fwd.hpp>

namespace nb {
    entt::registry &reg();

    entt::id_type build_etree(entt::id_type retree_id);
}