#pragma once

#include <newbase/res/fwd.h>
#include <entt/resource/cache.hpp>
#include <entt/resource/resource.hpp>
#include <ryml.hpp>

namespace nb {

class rmanager {
public:
    rmanager();

    bool configure(const char *scanpath = nullptr);
    entt::id_type resolve(const char *path);

    entt::resource<rsprite> get_sprite(entt::id_type id, bool forceload = false);
    entt::resource<rtexture> get_texture(entt::id_type id, bool forceload = false);
};

rmanager& rman();

}

