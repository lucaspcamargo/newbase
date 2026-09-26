#pragma once

#include <newbase/res/fwd.hpp>
#include <newbase/res/resource.hpp>
#include <newbase/res/storage/handle.hpp>
#include <newbase/res/vfs.hpp>
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
    void teardown();

    bool known(entt::id_type id);

    // Erase expired weak_ptr entries from the cache. Call periodically to keep memory tidy.
    void prune_cache();
    bool read_all_sync(entt::id_type id, std::vector<char> &dst, bool zero_terminate = false) const;
    bool read_partial_sync(entt::id_type id, std::size_t offset, std::size_t size, std::vector<char> &dst) const;
    bool write_all_sync(entt::id_type id, const void *data, std::size_t size);

    // Serialize a resource back to its underlying storage using the type's registered saver.
    bool save_resource(nb::resource* res);

    const std::unordered_map<entt::id_type, rmanager::asset_handle>& handles() const;
    const vfs_tree& vfs() const;
    vfs_tree build_vfs_tree() const;

    // Creates a resource, when not in the cache, and adds it to the cache.
    // When already in cache, returns the cached instance instead.
    // Does not load the resource.
    std::shared_ptr<nb::resource> create(entt::id_type type_id, entt::id_type asset_id);

    // Ensures resource is created and cached, if possible, via create()
    // Then loads t synchronously.
    // If there are background loading operation on this same resource,
    // waits on them to finish.
    std::shared_ptr<nb::resource> load_sync(entt::id_type type_id, entt::id_type asset_id);

    // Create and load a fresh resource from storage, without reading or updating the cache.
    // This is an independent instance of the resource that is loaded synchronously as far as possible.
    // Be careful with using this, if a resource updates any kind of global or system state via
    // its own id.
    std::shared_ptr<nb::resource> load_nocache(entt::id_type type_id, entt::id_type asset_id);

    // Typed convenience wrappers
    template<typename T>
    std::shared_ptr<T> create(entt::id_type asset_id)
    {
        return std::static_pointer_cast<T>(create(entt::resolve<T>().id(), asset_id));
    }
    template<typename T>
    std::shared_ptr<T> load_sync(entt::id_type asset_id)
    {
        return std::static_pointer_cast<T>(load_sync(entt::resolve<T>().id(), asset_id));
    }
    template<typename T>
    std::shared_ptr<T> load_nocache(entt::id_type asset_id)
    {
        return std::static_pointer_cast<T>(load_nocache(entt::resolve<T>().id(), asset_id));
    }


private:
    rmanager_p *_d {nullptr};
};

rmanager& rman();

}
