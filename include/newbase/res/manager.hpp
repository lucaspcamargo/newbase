#pragma once

#include "entt/core/fwd.hpp"
#include "newbase/res/async_load.hpp"
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

    // Erase expired weak_ptr entries from the cache.
    void prune_cache();

    // vfs data loading functions. Do not use for subresource (path/res?subres) hashes at is makes no sense
    bool read_all_sync(entt::id_type id, std::vector<char> &dst, bool zero_terminate = false) const;
    bool read_partial_sync(entt::id_type id, std::size_t offset, std::size_t size, std::vector<char> &dst) const;
    bool write_all_sync(entt::id_type id, const void *data, std::size_t size);

    // Serialize a resource back to its underlying storage using the type's registered saver.
    bool save_resource(nb::resource* res);

    // vfs query and management
    const std::unordered_map<entt::id_type, rmanager::asset_handle>& handles() const;
    const vfs_tree& vfs() const;
    vfs_tree build_vfs_tree() const;

    /// Announce the full path of a potential subresource ("path/res?subres")
    /// This allows a subresource to know of its parent with a vfs query of its own hash.
    /// Useful for mainly two things:
    /// - parent resources may register their subresources with the vfs, when preloaded.
    /// - deserialization mechanisms that may store references to subresources.
    ///   For these cases, the full path is required to know where the resource comes from.
    /// Subresources whose parents haven't yet been preloaded (a mesh from a gltf, for example),
    /// depend on this registration, since they only know of their own, unregistered hash.
    /// It is also safe to pass the path of a normal resource, which is a no-op.
    /// @returns asset id of subresource if any, otherwise entt::null_t
    const entt::id_type note_subresource(std::string_view uri);

    // Creates a resource, when not in the cache, and adds it to the cache.
    // When already in cache, returns the cached instance instead.
    // Does not load the resource.
    // Note that this does not work for subresources that haven't been discovered yet.
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

    // creates a new synchronous resource loaading job
    // if the engine is not in single-thread mode the job runs on a worker thread
    // this will load any resource's dependencies during the load process as well
    res::load::task_handle load_async(entt::id_type type_id, entt::id_type asset_id);

    // gets the current status of an async load job
    res::load::task_status async_status(res::load::task_handle);

    // cancel an sync loading job
    // all resource refences will be dropped during a cancelled job's stepping
    // the task handle will immediately become invalid for queries
    bool async_cancel(res::load::task_handle);

    // collects the results of an async load job
    // this must always be done or stale resource references will persist
    // (unless the async task is cancelled)
    bool async_complete(res::load::task_handle hnd, res::load::task_results &results);

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

    // go over the resource cache and log any resources that are still alive
    void leak_check();

private:
    rmanager_p *_d {nullptr};
};

rmanager& rman();

}
