#include "entt/core/fwd.hpp"
#include "entt/entity/entity.hpp"
#include <cstdint>
#include <newbase/res/manager.hpp>
#include <newbase/res/async_load.hpp>
#include <newbase/res/vfs.hpp>
#include <newbase/res/storage/interface.hpp>
#include <newbase/res/storage/sdl_file.hpp>
#include <newbase/res/storage/sdl_storage.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/nb_config.h>
#include <newbase/log.hpp>
#include <newbase/utility/strings.hpp>
#ifdef NEWBASE_USE_XDG_DATA_DIRS
#include <newbase/utility/xdg.h>
#endif

#include <entt/entt.hpp>
#include <SDL3/SDL_storage.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_init.h>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <cstdlib>
#include <mutex>

namespace nb{

struct rmanager_p {
    // weak cache: resource_type_id -> { asset_id -> weak_ptr<resource> }
    // entries expire automatically when no live shared_ptr references remain
    std::mutex cache_mtx;
    std::unordered_map<entt::id_type,
        std::unordered_map<entt::id_type, std::weak_ptr<nb::resource>>> caches;

    std::vector<std::unique_ptr<res_storage::storage_interface>> storage_interfaces;
    std::unordered_map<entt::id_type, res_storage::asset_handle> asset_handles;

    vfs_tree vfs;

    res::load::worker async_worker;
};

static rmanager _rman_inst;
rmanager& rman() {return _rman_inst;}

rmanager::rmanager()
{
    _d = new rmanager_p();
    _d->async_worker.run();
    log::info("[rmanager] async worker started");
}

rmanager::~rmanager()
{
    delete _d;
}

void rmanager::clear()
{
    log::info("[rmanager] clearing resource pools and storage interfaces");
    {
        std::lock_guard _{_d->cache_mtx};
        _d->caches.clear();
    }
    _d->asset_handles.clear();
    _d->storage_interfaces.clear();
    _d->vfs = vfs_tree{};
}

bool rmanager::configure(const ryml::NodeRef &config)
{
    log::info("[rmanager] configuring...");
    (void) config;
    bool use_sdl_file = false;
#ifdef ANDROID
    log::info("[rmanager] using SDL FileStorage on Android");
    use_sdl_file = true;
#endif
    std::string base_location {NEWBASE_DEFAULT_RES_PREFIX};
#ifdef NEWBASE_USE_XDG_DATA_DIRS
    if(_nb_xdg_data_dir_found())
    {
        base_location = _nb_xdg_data_dirname_get() + std::string{"/"} + base_location;
    }
#endif

    if(use_sdl_file)
    {
        auto sfile = std::make_unique<res_storage::sdl_file>(ryml::ConstNodeRef{}, base_location);
        _d->storage_interfaces.emplace_back(std::move(sfile));
    }
    else
    {
        auto sstorage = std::make_unique<res_storage::sdl_storage>(ryml::ConstNodeRef{}, base_location);
        _d->storage_interfaces.emplace_back(std::move(sstorage));
    }

    int sintf_idx = 0;
    for(auto &sintf : _d->storage_interfaces)
    {
        bool has_index = sintf->has_index();
        bool scannable = sintf->scannable();
        log::info("[rmanager] storage interface #%d: scannable=%s, has_index=%s",
                  sintf_idx,
                  scannable? "yes" : "no",
                  has_index? "yes" : "no");

        auto handles = sintf->get_handles(scannable, has_index);

        for(auto &handle : handles)
        {
            log::verb("[rmanager] registered asset: %s (%zub)", handle.path.c_str(), handle.size);
            handle.storage_interface_idx = sintf_idx;
            _d->asset_handles.insert(std::make_pair(handle.id, handle));
        }

        sintf_idx++;
    }

    _d->vfs = build_vfs_tree();

    return true;
}

void rmanager::teardown()
{
    clear();

    log::info("[rmanager] stopping async worker");
    _d->async_worker.stop();
    log::info("[rmanager] async worker stopped");
}

void rmanager::prune_cache()
{
    for (auto& [type_id, type_cache] : _d->caches)
    {
        for (auto it = type_cache.begin(); it != type_cache.end(); )
        {
            if (it->second.expired())
                it = type_cache.erase(it);
            else
                ++it;
        }
    }
}

bool rmanager::known(entt::id_type id)
{
    return _d->asset_handles.find(id) != _d->asset_handles.end();
}

bool rmanager::read_all_sync(entt::id_type id, std::vector<char> &dst, bool zero_terminate) const
{
    auto it = _d->asset_handles.find(id);
    if(it == _d->asset_handles.end())
    {
        log::error("[rmanager] unknown asset id: %x", id);
        return false;
    }
    const auto &handle = it->second;
    int sintf_idx = handle.storage_interface_idx;
    if(sintf_idx < 0 || sintf_idx >= static_cast<int>(_d->storage_interfaces.size()))
    {
        log::error("[rmanager] invalid storage interface index %d for asset id: %x", sintf_idx, id);
        return false;
    }
    auto &sintf = _d->storage_interfaces[sintf_idx];
    return sintf->read_all_sync(handle, dst, zero_terminate);
}

bool rmanager::save_resource(nb::resource* res)
{
    if (!res)
    {
        log::error("[rmanager] save_resource: null resource");
        return false;
    }
    auto mtype = entt::resolve(res->type_id());
    if (!mtype)
    {
        log::error("[rmanager] save_resource: unregistered type %x", res->type_id());
        return false;
    }
    const rtti::type_info* info = mtype.custom().operator rtti::type_info*();
    if (!info || !info->saver_fn)
    {
        log::error("[rmanager] save_resource: no saver registered for type %x", res->type_id());
        return false;
    }
    return info->saver_fn(res);
}

bool rmanager::read_partial_sync(entt::id_type id, std::size_t offset, std::size_t size, std::vector<char> &dst) const
{
    auto it = _d->asset_handles.find(id);
    if(it == _d->asset_handles.end())
    {
        log::error("[rmanager] read_partial_sync: unknown asset id: %x", id);
        return false;
    }
    const auto &handle = it->second;
    int sintf_idx = handle.storage_interface_idx;
    if(sintf_idx < 0 || sintf_idx >= static_cast<int>(_d->storage_interfaces.size()))
    {
        log::error("[rmanager] read_partial_sync: invalid storage index %d for asset: %x", sintf_idx, id);
        return false;
    }
    return _d->storage_interfaces[sintf_idx]->read_partial_sync(handle, offset, size, dst);
}

bool rmanager::write_all_sync(entt::id_type id, const void *data, std::size_t size)
{
    auto it = _d->asset_handles.find(id);
    if (it == _d->asset_handles.end())
    {
        log::error("[rmanager] write_all_sync: unknown asset id: %x", id);
        return false;
    }
    const auto &handle = it->second;
    int sintf_idx = handle.storage_interface_idx;
    if (sintf_idx < 0 || sintf_idx >= static_cast<int>(_d->storage_interfaces.size()))
    {
        log::error("[rmanager] write_all_sync: invalid storage interface index %d for asset: %x", sintf_idx, id);
        return false;
    }
    auto &sintf = _d->storage_interfaces[sintf_idx];
    if (!sintf->writable())
    {
        log::error("[rmanager] write_all_sync: storage is not writable for asset: %s", handle.path.c_str());
        return false;
    }
    return sintf->write_all_sync(handle, data, size);
}

const std::unordered_map<entt::id_type, rmanager::asset_handle>& rmanager::handles() const
{
    return _d->asset_handles;
}

const vfs_tree& rmanager::vfs() const
{
    return _d->vfs;
}

std::shared_ptr<nb::resource> rmanager::create(entt::id_type type_id, entt::id_type asset_id)
{
    std::lock_guard<std::mutex> lock(_d->cache_mtx);
    auto &type_cache = _d->caches[type_id];

    auto it = type_cache.find(asset_id);
    bool replace_it = false;
    if (it != type_cache.end())
    {
        if (auto res = it->second.lock())
        {
            return res; // Return existing instance regardless of state
        }
        replace_it = true;
    }

    auto mtype = entt::resolve(type_id);
    if (!mtype)
    {
        log::error("[rmanager] unregistered resource type: %x", type_id);
        return nullptr;
    }

    const rtti::type_info *info = mtype.custom().operator rtti::type_info*();
    if (!info || info->type_class != rtti::TYPE_CLASS_RESOURCE || !info->data.resource.factory_fn)
    {
        log::error("[rmanager] resource type missing factory: %x", type_id);
        return nullptr;
    }

    auto res = info->data.resource.factory_fn(asset_id);
    if (!res)
    {
        log::error("[rmanager] failed to instantiate resource shell: %x", type_id);
        return nullptr;
    }

    if(replace_it)
        it->second = res;
    else
        type_cache[asset_id] = res;

    return res;
}

std::shared_ptr<nb::resource> rmanager::load_sync(entt::id_type type_id, entt::id_type asset_id)
{
    auto res = create(type_id, asset_id);
    if (!res)
    {
        return nullptr;
    }

    // Force synchronous load or wait for active worker thread to finish
    res->force_load_sync();

    // If load failed, don't hand out broken handle
    if (res->is_failed())
    {
        return nullptr;
    }

    return res;
}


std::shared_ptr<nb::resource> rmanager::load_nocache(entt::id_type type_id, entt::id_type asset_id)
{
    auto mtype = entt::resolve(type_id);
    if (!mtype)
    {
        log::error("[rmanager] unregistered resource type: %x", type_id);
        return nullptr;
    }

    const rtti::type_info *info = mtype.custom().operator rtti::type_info*();
    if (!info || info->type_class != rtti::TYPE_CLASS_RESOURCE || !info->data.resource.factory_fn)
    {
        log::error("[rmanager] resource type missing factory: %x", type_id);
        return nullptr;
    }

    auto res = info->data.resource.factory_fn(asset_id);
    if (!res)
    {
        log::error("[rmanager] failed to instantiate resource shell: %x", type_id);
        return nullptr;
    }

    // Force synchronous load or wait for active worker thread to finish
    res->force_load_sync();

    // If load failed, don't hand out broken handle
    if (res->is_failed())
    {
        return nullptr;
    }

    return res;
}

res::load::task_handle rmanager::load_async(entt::id_type type_id, entt::id_type asset_id)
{
    res::load::task_descriptor desc {};
    desc.entries.push_back(res::load::task_descriptor::entry {type_id, asset_id});
    return _d->async_worker.start(desc);
}

res::load::task_status rmanager::async_status(res::load::task_handle hnd)
{
    return _d->async_worker.status(hnd);
}

bool rmanager::async_cancel(res::load::task_handle hnd)
{
    return _d->async_worker.cancel(hnd);
}

bool rmanager::async_complete(res::load::task_handle hnd, res::load::task_results &results)
{
    results = _d->async_worker.complete(hnd);
    return results.final_status.done;
}

vfs_tree rmanager::build_vfs_tree() const
{
    vfs_tree tree;
    auto& reg = tree.registry();

    std::unordered_map<std::string, entt::entity> path_to_entity;
    path_to_entity.reserve(_d->asset_handles.size() * 2);
    path_to_entity[""] = tree.root();

    for (auto& [id, handle] : _d->asset_handles)
    {
        std::string_view sv = handle.path;
        std::string accumulated;
        entt::entity parent = tree.root();

        while (!sv.empty())
        {
            auto slash = sv.find('/');
            bool is_last = (slash == std::string_view::npos);
            std::string_view segment = is_last ? sv : sv.substr(0, slash);

            if (!accumulated.empty()) accumulated += '/';
            accumulated.append(segment.data(), segment.size());

            auto it = path_to_entity.find(accumulated);
            if (it != path_to_entity.end())
            {
                parent = it->second;
            }
            else
            {
                entt::entity e = reg.create();
                reg.emplace<vfs_node>(e, std::string{segment}, accumulated);
                if (is_last)
                    reg.emplace<res_storage::asset_handle>(e, handle);
                reg.get<vfs_node>(parent).children.push_back(e);
                path_to_entity.emplace(accumulated, e);
                parent = e;
            }

            if (is_last) break;
            sv = sv.substr(slash + 1);
        }
    }

    return tree;
}


const entt::id_type rmanager::note_subresource(std::string_view uri)
{
    const auto sz = uri.length();
    if(!sz)
    {
        log::warn("[res] manager: vfs_note_subresource: empty path!");
        return entt::null_t{};
    }

    // check if already known
    entt::id_type full_hash = entt::hashed_string{uri.data(), sz}.value();
    if(auto hnd_it = _d->asset_handles.find(full_hash); hnd_it != _d->asset_handles.end())
    {
        return hnd_it->second.parent_id? full_hash : entt::null_t{};
    }

    const auto query_marker_idx = uri.find("?");
    if(query_marker_idx == uri.npos)
        return entt::null_t{}; // not a subresource


    if(query_marker_idx == 0)
    {
        log::error("[res] manager: vfs_note_subresource: subresource with no parent: '%.*s'", (int)sz, uri.data());
        return entt::null_t{};
    }
    if(query_marker_idx == sz - 1)
    {
        log::warn("[res] manager: vfs_note_subresource: subresource with empty query. Technically valid but weird: '%.*s'", (int)sz, uri.data());
    }

    std::string parent_path {uri.substr(0, query_marker_idx)};
    entt::id_type parent_hash = entt::hashed_string{parent_path.c_str()}.value();
    if(!known(parent_hash))
    {
        log::error("[res] manager: vfs_note_subresource: uknown parent: '%.*s'", (int)sz, uri.data());
        return entt::null_t{};
    }

    std::string query {uri.substr(query_marker_idx+1)};

    _d->asset_handles.emplace( full_hash, asset_handle {
        .id = full_hash,
        .name = query,
        .path = std::string{uri},
        .parent_id = parent_hash
    });

    log::info("[res] manager: vfs_note_subresource: registered 0x%08x: '%s' (0x%08x) ? '%s'",
              full_hash, parent_path.c_str(), parent_hash, query.c_str());
    // now the subresource, with parent reference, is known to the resource system

    return full_hash;
}

void rmanager::leak_check()
{
    for (auto& [type_id, type_cache] : _d->caches)
    {
        for (auto &[id, w_res] : type_cache)
        {
            auto res = w_res.lock();
            if(!res)
                continue;

            std::string type_name {res->meta_type().info().name()};
            std::string res_name {};

            auto vfs_entry = handles().find(res->id());
            if(vfs_entry!=handles().end())
            {
                auto &hnd = vfs_entry->second;
                res_name = hnd.path;
            }
            else
            {
                char buffer[20];
                buffer[0] = '0';
                buffer[1] = 'x';
                auto [p, ec] = std::to_chars(buffer + 2, buffer + sizeof(buffer), (uintptr_t)res.get(), 16);
                res_name = {buffer, (size_t)(p-buffer)};
            }

            log::warn("[rmanager] leak_check: resource still alive: '%s' (0x%08x), type '%s' (0x%08x)", res_name.c_str(), res->id(), type_name.c_str(), res->type_id());
        }
    }
}

}
