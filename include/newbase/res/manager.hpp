#pragma once

#include <newbase/res/fwd.hpp>
#include <newbase/res/resource.hpp>
#include <newbase/res/storage/handle.hpp>
#include <newbase/res/vfs.hpp>
#include <entt/resource/resource.hpp>
#include <entt/resource/cache.hpp>
#include <ryml.hpp>
#include <memory>
#include <string_view>
#include <unordered_map>

struct SDL_Storage;

namespace nb {

struct rmanager_p;

class rmanager final {
public:
    using asset_handle = res_storage::asset_handle;

    rmanager();
    ~rmanager();

    bool configure(const ryml::NodeRef &config);
    void clear();

    bool known(entt::id_type id);
    bool read_all_sync(entt::id_type id, std::vector<char> &dst, bool zero_terminate = false) const;
    bool write_all_sync(entt::id_type id, const void *data, std::size_t size);

    // Serialize a resource back to its underlying storage using the type's registered saver.
    bool save_resource(nb::resource* res);

    const std::unordered_map<entt::id_type, rmanager::asset_handle>& handles() const;
    const vfs_tree& vfs() const;
    vfs_tree build_vfs_tree() const;

    // Generic load: looks up the registered loader via RTTI
    std::shared_ptr<nb::resource> get(entt::id_type type_id, entt::id_type asset_id, bool forceload = false);

    // Typed convenience wrapper
    template<typename T>
    std::shared_ptr<T> get(entt::id_type asset_id, bool forceload = false)
    {
        return std::static_pointer_cast<T>(get(entt::resolve<T>().id(), asset_id, forceload));
    }

    // Backwards-compatible typed accessors
    entt::resource<retree>   get_etree  (entt::id_type id, bool forceload = false);
    entt::resource<rsprite>  get_sprite (entt::id_type id, bool forceload = false);
    entt::resource<rtexture> get_texture(entt::id_type id, bool forceload = false);
    entt::resource<rscript>  get_script (entt::id_type id, bool forceload = false);
    entt::resource<rvorbis>  get_vorbis (entt::id_type id, bool forceload = false);
    entt::resource<ryaml>    get_yaml   (entt::id_type id, bool forceload = false);

private:
    rmanager_p *_d {nullptr};
};

rmanager& rman();

}
