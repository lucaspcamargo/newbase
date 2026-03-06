#include <newbase/res/manager.hpp>
#include <newbase/res/loaders.hpp>
#include <newbase/res/storage/interface.hpp>
#include <newbase/res/storage/sdl_file.hpp>
#include <newbase/res/storage/sdl_storage.hpp>
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

static entt::resource_cache<retree, rloader_etree> _cache_etree{};
static entt::resource_cache<rsprite, rloader_sprite> _cache_sprite{};
static entt::resource_cache<rtexture, rloader_texture> _cache_texture{};
static entt::resource_cache<rscript, rloader_script> _cache_script{};
static entt::resource_cache<rvorbis, rloader_vorbis> _cache_vorbis{};
static entt::resource_cache<ryaml, rloader_yaml> _cache_yaml{};

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

    // release asset handles
    _asset_handles.clear();

    // release storage interfaces
    _storage_interfaces.clear();
}

bool rmanager::configure(const ryml::NodeRef &config)
{
    log::info("[rmanager] configuring...");
    // WIP new asset infrastructure
    // for now we don't use config, just open appropriate storage according to platform :)
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

    // Now, we manage our storage interfaces
    int sintf_idx = 0;
    for(auto &sintf : _storage_interfaces)
    {
        bool has_index = sintf->has_index();
        bool scannable = sintf->scannable();
        log::info("[rmanager] storage interface #%d: scannable=%s, has_index=%s",
                  sintf_idx,
                  scannable? "yes" : "no",
                  has_index? "yes" : "no");

        // ask for asset handles
        // if we have an index, use it
        // otherwise, try scanning if possible
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

bool rmanager::read_all_sync(entt::id_type id, std::vector<char> &dst, bool zero_terminate) const // read all data into byte vector
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

template <typename Cache>
static inline std::pair<typename Cache::iterator, bool> load_maybe_force(Cache &cache, entt::id_type id, bool forceload)
{
    return (C4_UNLIKELY(forceload)? cache.force_load(id, id)
                                 : cache.load(id, id));
}


entt::resource<retree> rmanager::get_etree(entt::id_type id, bool forceload)
{
    auto result = load_maybe_force(_cache_etree, id, forceload);
    bool loaded = result.second;
    assert(loaded);
    return result.first->second;
}

entt::resource<rsprite> rmanager::get_sprite(entt::id_type id, bool forceload)
{
    return (C4_UNLIKELY(forceload)? _cache_sprite.force_load(id, id)
                                 : _cache_sprite.load(id, id)).first->second;
}

entt::resource<rtexture> rmanager::get_texture(entt::id_type id, bool forceload)
{
    return (C4_UNLIKELY(forceload)? _cache_texture.force_load(id, id)
                                 : _cache_texture.load(id, id)).first->second;
}

entt::resource<rscript> rmanager::get_script(entt::id_type id, bool forceload)
{
    return (C4_UNLIKELY(forceload)? _cache_script.force_load(id, id)
                                 : _cache_script.load(id, id)).first->second;
}

entt::resource<rvorbis> rmanager::get_vorbis(entt::id_type id, bool forceload)
{
    return (C4_UNLIKELY(forceload)? _cache_vorbis.force_load(id, id)
                                 : _cache_vorbis.load(id, id)).first->second;
}

entt::resource<ryaml> rmanager::get_yaml(entt::id_type id, bool forceload)
{
    return (C4_UNLIKELY(forceload)? _cache_yaml.force_load(id, id)
                                 : _cache_yaml.load(id, id)).first->second;
}

}
