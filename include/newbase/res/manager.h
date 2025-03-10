#pragma once

#include <newbase/res/fwd.h>
#include <entt/resource/cache.hpp>
#include <entt/resource/resource.hpp>
#include <ryml.hpp>

namespace nb {

class rmanager final {
public:
    rmanager();
    ~rmanager();

    bool configure(const ryml::NodeRef &config);

    bool known(entt::id_type id); // whether this resource hash is known
    bool read_all_sync(entt::id_type id, std::vector<char> &dst, bool zero_term = false) const; // read all data into byte vector

    entt::resource<retree> get_etree(entt::id_type id, bool forceload = false);
    entt::resource<rsprite> get_sprite(entt::id_type id, bool forceload = false);
    entt::resource<rtexture> get_texture(entt::id_type id, bool forceload = false);
};

rmanager& rman();

}

