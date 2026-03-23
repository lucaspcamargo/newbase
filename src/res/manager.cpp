#include <newbase/res/manager.hpp>
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

static rmanager _rman_inst;
rmanager& rman() {return _rman_inst;}

// dynamic cache: resource_type_id -> { asset_id -> shared_ptr<resource> }
static std::unordered_map<entt::id_type,
    std::unordered_map<entt::id_type, std::shared_ptr<nb::resource>>> _caches;

// NEW asset infrastructure
static std::vector<std::unique_ptr<res_storage::storage_interface>> _storage_interfaces {};
static std::unordered_map<entt::id_type, res_storage::asset_handle> _asset_handles {};

rmanager::rmanager()
{
    // static initialization
}

rmanager::~rmanager()
{
    // called in exit handlers!
}

void rmanager::clear()
{
    log::info("[rmanager] clearing resource pools and storage interfaces");

    _caches.clear();
    _asset_handles.clear();
    _storage_interfaces.clear();
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
        _storage_interfaces.emplace_back(std::move(sfile));
    }
    else
    {
        auto sstorage = std::make_unique<res_storage::sdl_storage>(ryml::ConstNodeRef{}, base_location);
        _storage_interfaces.emplace_back(std::move(sstorage));
    }

    int sintf_idx = 0;
    for(auto &sintf : _storage_interfaces)
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
            _asset_handles.insert(std::make_pair(handle.id, handle));
        }

        sintf_idx++;
    }

    return true;
}

bool rmanager::known(entt::id_type id)
{
    return _asset_handles.find(id) != _asset_handles.end();
}

bool rmanager::read_all_sync(entt::id_type id, std::vector<char> &dst, bool zero_terminate) const
{
    auto it = _asset_handles.find(id);
    if(it == _asset_handles.end())
    {
        log::error("[rmanager] unknown asset id: %x", id);
        return false;
    }
    const auto &handle = it->second;
    int sintf_idx = handle.storage_interface_idx;
    if(sintf_idx < 0 || sintf_idx >= static_cast<int>(_storage_interfaces.size()))
    {
        log::error("[rmanager] invalid storage interface index %d for asset id: %x", sintf_idx, id);
        return false;
    }
    auto &sintf = _storage_interfaces[sintf_idx];
    return sintf->read_all_sync(handle, dst, zero_terminate);
}

const std::unordered_map<entt::id_type, rmanager::asset_handle>& rmanager::handles() const
{
    return _asset_handles;
}

std::shared_ptr<nb::resource> rmanager::get(entt::id_type type_id, entt::id_type asset_id, bool forceload)
{
    auto &type_cache = _caches[type_id];

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

}
