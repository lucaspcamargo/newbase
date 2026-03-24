#include <newbase/res/manager.hpp>
#include <newbase/res/vfs.hpp>
#include <newbase/res/loaders.hpp>
#include <newbase/res/etree.hpp>
#include <newbase/res/sprite.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/script.hpp>
#include <newbase/res/vorbis.hpp>
#include <newbase/res/wav.hpp>
#include <newbase/res/yaml.hpp>
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
#include <iostream> // temp

namespace nb{

struct rmanager_p {
    // dynamic cache: resource_type_id -> { asset_id -> shared_ptr<resource> }
    std::unordered_map<entt::id_type,
        std::unordered_map<entt::id_type, std::shared_ptr<nb::resource>>> caches;

    std::vector<std::unique_ptr<res_storage::storage_interface>> storage_interfaces;
    std::unordered_map<entt::id_type, res_storage::asset_handle> asset_handles;

    vfs_tree vfs;
};

static rmanager _rman_inst;
rmanager& rman() {return _rman_inst;}

rmanager::rmanager()
{
    _d = new rmanager_p();
}

rmanager::~rmanager()
{
    // called in exit handlers!
    delete _d;
}

void rmanager::clear()
{
    log::info("[rmanager] clearing resource pools and storage interfaces");

    _d->caches.clear();
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
            log::info("[rmanager] registered asset: %s (%zub)", handle.path.c_str(), handle.size);
            handle.storage_interface_idx = sintf_idx;
            _d->asset_handles.insert(std::make_pair(handle.id, handle));
        }

        sintf_idx++;
    }

    _d->vfs = build_vfs_tree();

    return true;
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

const std::unordered_map<entt::id_type, rmanager::asset_handle>& rmanager::handles() const
{
    return _d->asset_handles;
}

const vfs_tree& rmanager::vfs() const
{
    return _d->vfs;
}

std::shared_ptr<nb::resource> rmanager::get(entt::id_type type_id, entt::id_type asset_id, bool forceload)
{
    auto &type_cache = _d->caches[type_id];

    if (!forceload)
    {
        auto it = type_cache.find(asset_id);
        if (it != type_cache.end())
            return it->second;
    }

    auto mtype = entt::resolve(type_id);
    if (!mtype)
    {
        log::error("[rmanager] unregistered resource type: %x", type_id);
        return nullptr;
    }
    const rtti::type_info *info = mtype.custom().operator rtti::type_info*();
    if (!info || info->type_class != rtti::TYPE_CLASS_RESOURCE || !info->loader_fn)
    {
        log::error("[rmanager] resource type has no loader: %x", type_id);
        return nullptr;
    }

    auto res = info->loader_fn(asset_id);
    if (res)
        type_cache[asset_id] = res;
    return res;
}

// Backwards-compatible typed accessors

entt::resource<retree> rmanager::get_etree(entt::id_type id, bool forceload)
{
    return entt::resource<retree>{std::static_pointer_cast<retree>(
        get(entt::resolve<retree>().id(), id, forceload))};
}

entt::resource<rsprite> rmanager::get_sprite(entt::id_type id, bool forceload)
{
    return entt::resource<rsprite>{std::static_pointer_cast<rsprite>(
        get(entt::resolve<rsprite>().id(), id, forceload))};
}

entt::resource<rtexture> rmanager::get_texture(entt::id_type id, bool forceload)
{
    return entt::resource<rtexture>{std::static_pointer_cast<rtexture>(
        get(entt::resolve<rtexture>().id(), id, forceload))};
}

entt::resource<rscript> rmanager::get_script(entt::id_type id, bool forceload)
{
    return entt::resource<rscript>{std::static_pointer_cast<rscript>(
        get(entt::resolve<rscript>().id(), id, forceload))};
}

entt::resource<rvorbis> rmanager::get_vorbis(entt::id_type id, bool forceload)
{
    return entt::resource<rvorbis>{std::static_pointer_cast<rvorbis>(
        get(entt::resolve<rvorbis>().id(), id, forceload))};
}

entt::resource<ryaml> rmanager::get_yaml(entt::id_type id, bool forceload)
{
    return entt::resource<ryaml>{std::static_pointer_cast<ryaml>(
        get(entt::resolve<ryaml>().id(), id, forceload))};
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

}
