#pragma once

#include <newbase/res/fwd.hpp>
#include <newbase/res/storage/descriptor.hpp>
#include <entt/resource/cache.hpp>
#include <entt/resource/resource.hpp>
#include <ryml.hpp>
#include <string_view>
#include <unordered_map>

struct SDL_Storage;

namespace nb {

class rmanager final {
public:
    using descriptor = res_storage::descriptor;

    rmanager();
    ~rmanager();

    bool configure(const ryml::NodeRef &config);
    
    bool known(entt::id_type id); // whether this resource hash is known
    bool read_all_sync(entt::id_type id, std::vector<char> &dst, bool zero_term = false) const; // read all data into byte vector

    const std::unordered_map<entt::id_type, rmanager::descriptor>& descriptors() const;

    entt::resource<retree> get_etree(entt::id_type id, bool forceload = false);
    entt::resource<rsprite> get_sprite(entt::id_type id, bool forceload = false);
    entt::resource<rtexture> get_texture(entt::id_type id, bool forceload = false);
    entt::resource<rscript> get_script(entt::id_type id, bool forceload = false);
    entt::resource<rvorbis> get_vorbis(entt::id_type id, bool forceload = false);
    entt::resource<ryaml> get_yaml(entt::id_type id, bool forceload = false);


private:
    void register_file(SDL_Storage *storage, const char *path, size_t sz);
};

rmanager& rman();

}

