#include <newbase/res/manager.h>
#include <newbase/res/loaders.h>
#include <newbase/nb_config.h>

#include <entt/entt.hpp>
#include <SDL3/SDL_storage.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <iostream> // temp

namespace nb{

struct locator {
    std::string path{};             // corresponding path
    SDL_Storage* store{ nullptr };  // storage where data is from
};

static rmanager _rman_inst;
rmanager& rman() {return _rman_inst;}

static entt::resource_cache<retree, rloader_etree> _cache_etree{};
static entt::resource_cache<rsprite, rloader_sprite> _cache_sprite{};
static entt::resource_cache<rtexture, rloader_texture> _cache_texture{};

SDL_Storage *_store_title{ nullptr };

std::map<entt::id_type, locator> _pathmap;

rmanager::rmanager()
{
    
}

rmanager::~rmanager()
{
    _pathmap.clear();
    if(_store_title)
    {
        SDL_CloseStorage(_store_title);
        _store_title = nullptr;
    }
}

bool rmanager::configure(const ryml::NodeRef &config)
{
    // TODO rework with "source" plugins and use yaml config

    _store_title = SDL_OpenTitleStorage(NEWBASE_DEFAULT_RES_PREFIX, SDL_CreateProperties());
    if(!_store_title)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[rmanager] cannot title storage. Falling back to raw fs...");
        _store_title = SDL_OpenFileStorage(NEWBASE_DEFAULT_RES_PREFIX);
    }
    if(_store_title)
    {
        char ** vfiles = SDL_GlobStorageDirectory(_store_title, nullptr, nullptr, 0, nullptr);
        if(vfiles)
        {
            for(char **curr = vfiles; *curr; curr++)
            {

                SDL_PathInfo pinfo;
                if(!SDL_GetStoragePathInfo(_store_title, *curr, &pinfo))
                    continue;
                if(pinfo.type != SDL_PATHTYPE_FILE)
                    continue;
                
                std::string prefixed{"@"};
                bool absolute = (*curr)[0] == '/';
                prefixed.append( *curr + (absolute? 1 : 0)); // if path is absolute (usually emscripten, omit root)
                auto hash = entt::hashed_string(prefixed.c_str());
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rmanager] storage file: %s (%x)", prefixed.c_str(), hash.value());
                _pathmap.emplace(std::make_pair(hash.value(), locator{prefixed, _store_title}));
            }
        }
        else
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[rmanager] cannot enumerate title storage: %s", SDL_GetError());
            return false;
        }
    }
    else 
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "[rmanager] cannot open any title storage!");
        return false;
    }

    return true;
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
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rmanager] unregistered id: (%x)", id);
        return false;
    }

    const auto &loc = it->second;

    SDL_PathInfo info;
    std::string path = loc.path.substr(1);
    if(!SDL_GetStoragePathInfo(_store_title, path.c_str(), &info))
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rmanager] cannot stat: %s (%x)", path.c_str(), id);
        return false;
    }
    else
    {
        dst.resize(static_cast<size_t>(info.size)+(zero_terminate? 1 : 0));
        if(!SDL_ReadStorageFile(_store_title, path.c_str(), dst.data(), info.size))
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rmanager] cannot read: %s (%x)", path.c_str(), id);
            return false;
        }
        else
        {
            if(zero_terminate)
                dst[info.size] = '\0';
            //std::cerr << std::string(dst.data()) << std::endl;
            return true;
        }
    }  
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

}
