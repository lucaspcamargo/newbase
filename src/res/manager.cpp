#include <newbase/res/manager.h>
#include <newbase/res/loaders.h>
#include <newbase/nb_config.h>
#include <newbase/log.h>
#include <newbase/utility/strings.h>
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

SDL_Storage *_store_title{ nullptr };

static std::unordered_map<entt::id_type, rmanager::descriptor> _pathmap;

rmanager::rmanager()
{
    
}

rmanager::~rmanager()
{
    if(_store_title)
    {
        SDL_CloseStorage(_store_title);
        _store_title = nullptr;
    }
}

bool rmanager::configure(const ryml::NodeRef &config)
{
    // TODO rework with "source" plugins and use yaml config
#ifndef ANDROID
    std::string base_location {NEWBASE_DEFAULT_RES_PREFIX};
#ifdef NEWBASE_USE_XDG_DATA_DIRS
    if(_nb_xdg_data_dir_found())
    {
        base_location = _nb_xdg_data_dirname_get() + std::string{"/"} + base_location;
    }
#endif
    _store_title = SDL_OpenTitleStorage(base_location.c_str(), SDL_CreateProperties());
    if(!_store_title)
    {
        log::warn("[rmanager] cannot open title storage. Falling back to raw fs...");
        _store_title = SDL_OpenFileStorage(NEWBASE_DEFAULT_RES_PREFIX);
    }
#else
    // Always use FileStorage on Android, as asset reading is tied to RWOps
    _store_title = SDL_OpenFileStorage(NEWBASE_DEFAULT_RES_PREFIX);
    if(!_store_title) {
        log::critical("[rmanager] [android] cannot open file storage...");
        return false;
    }
#endif

    if(_store_title)
    {
        char ** vfiles = SDL_GlobStorageDirectory(_store_title, nullptr, nullptr, 0, nullptr);
        if(vfiles)
        {
            for(char **curr = vfiles; *curr; curr++)
            {
                Uint64 sz;
                if(SDL_GetStorageFileSize(_store_title, *curr, &sz))
                    register_file(_store_title, *curr, static_cast<size_t>(sz));
            }
            SDL_free(vfiles);
        }
        else
        {
            log::warn("[rmanager] cannot enumerate title storage: %s", SDL_GetError());
            log::warn("[rmanager] searching index file");
            std::string idx_path {"index.yaml"};
            Uint64 idx_len;
            std::vector<char> buf;
#ifndef ANDROID
            if (SDL_GetStorageFileSize(_store_title, idx_path.c_str(), &idx_len) && idx_len > 0)
            {
                buf.resize(static_cast<size_t>(idx_len));
                if (SDL_ReadStorageFile(_store_title, idx_path.c_str(), buf.data(), idx_len))
                {
#else
            void *data = SDL_LoadFile(NEWBASE_DEFAULT_RES_PREFIX"/index.yaml", &idx_len);
            if(data)
            {
                buf.resize(static_cast<size_t>(idx_len));
                memcpy(buf.data(), data, static_cast<size_t>(idx_len));
                {
#endif
                    auto tree = ryml::parse_in_place(buf.data());
                    assert(tree.rootref().has_children());
                    for(ryml::ConstNodeRef n : tree.rootref().cchildren())
                    {
                        std::string path;
                        size_t sz;
                        n[0] >> path;
                        n[1] >> sz;
                        register_file(_store_title, path.c_str(), sz);
                    }
                    return true;
                }
#ifndef ANDROID
                else 
                {
                    log::error("[rmanager] cannot open index.yaml!");
                    return false;
                }
#else
                SDL_free(data);
#endif
            }
            else
            {
                log::error("[rmanager] cannot stat index.yaml!");
                return false;
            }
        }
    }
    else 
    {
        log::critical("[rmanager] cannot open any title storage!");
        return false;
    }

    return true;
}

void rmanager::register_file(SDL_Storage *storage, const char *path, size_t sz)
{
    if(!path || !path[0])
        return;
    bool absolute = path[0] == '/';
    path += absolute? 1 : 0;
    auto hash = entt::hashed_string(path);
    log::info("[rmanager] storage file: %s (%x)", path, hash.value());
    _pathmap.insert(std::make_pair(hash.value(), rmanager::descriptor{path, sz, nullptr}));
}

bool rmanager::known(entt::id_type id)
{
    return _pathmap.find(id) != _pathmap.end();
}

bool rmanager::read_all_sync(entt::id_type id, std::vector<char> &dst, bool zero_terminate) const // read all data into byte vector
{
    auto it = _pathmap.find(id);
    if(it == _pathmap.end())
    {
        log::info("[rmanager] unregistered id: (%x)", id);
        return false;
    }

    const auto &loc = it->second;

    const std::string &path = loc.path;
    dst.resize(static_cast<size_t>(loc.size)+(zero_terminate? 1 : 0));
#ifndef ANDROID
    if(!SDL_ReadStorageFile(_store_title, path.c_str(), dst.data(), loc.size))
#else
    std::string fpath {NEWBASE_DEFAULT_RES_PREFIX};
    fpath += "/";
    fpath += path;
    Uint64 sz;
    void *data = SDL_LoadFile(fpath.c_str(), &sz);
    bool ok = data && (sz == loc.size);
    if(data)
    {
        if(ok)
            memcpy(dst.data(), data, sz);
        SDL_free(data);
    }
    if(!ok)
#endif
    {
        log::info("[rmanager] cannot read: %s (%x)", path.c_str(), id);
        dst.resize(0);
        return false;
    }
    else
    {
        if(zero_terminate)
            dst[loc.size] = '\0';
        return true;
    }  
}

const std::unordered_map<entt::id_type, rmanager::descriptor>& rmanager::descriptors() const
{
    return _pathmap;
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
