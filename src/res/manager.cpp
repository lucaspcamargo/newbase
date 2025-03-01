#include <newbase/res/manager.h>
#include <newbase/res/loaders.h>

#include <entt/entt.hpp>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

namespace nb{

static rmanager _rman_inst;
rmanager& rman() {return _rman_inst;}

static entt::resource_cache<rsprite, rloader_sprite> _cache_sprite{};
static entt::resource_cache<rtexture, rloader_texture> _cache_texture{};

std::map<entt::hashed_string, std::string> _pathmap;

rmanager::rmanager()
{
    
}

bool rmanager::configure(const char *scanpath)
{
    // we need to build the map from resource path hashes to the actual paths
    // keeping it simple for now

    if(!scanpath)
        return true;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rmanager] scanning: %s", scanpath);\

    char ** files = SDL_GlobDirectory(scanpath, nullptr, 0, nullptr);
    if(files)
    {
        for(char **curr = files; *curr; curr++)
        {
            auto hash = entt::hashed_string(*curr);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rmanager] saw: %s (%x)", curr);
            _pathmap.emplace(hash, *curr);
        }

        SDL_free(files);
        return true;
    }
    else return false;
}
entt::id_type rmanager::resolve(const char *path)
{
    return entt::tombstone;
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
