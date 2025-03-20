#include <newbase/res/loaders.h>
#include <newbase/res/manager.h>
#include <newbase/res/etree.h>
#include <newbase/res/texture.h>
#include <newbase/res/sprite.h>
#include <newbase/res/script.h>

#include <SDL3/SDL.h>
#include <stb_image.h>
#include <lua.h>

#include <memory>


namespace nb {

    // etree loader
    rloader_etree::result_type rloader_etree::operator()(entt::id_type id) const
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rloader_etree] loading: %x", id);
        auto ret = std::make_shared<retree>();
        auto read_success = rman().read_all_sync(id, ret->data, true);
        if(read_success)
        {
            ret->tree = ryml::parse_in_place(c4::to_substr(ret->data.data()));
            ret->valid = ret->tree.rootref().is_seq();
        }
        else
        {
            return nullptr; // let go of allocated resource, fail to load
        }
        return ret;
    }

    // sprite loader
    rloader_sprite::result_type rloader_sprite::operator()(entt::id_type id) const
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rloader_sprite] loading: %x", id);
        // TODO have sprite data file?
        // for now just load texture
        auto ret = std::make_shared<rsprite>();
        ret->id_tex = id;
        return ret;
    }

    // texture loader
    rloader_texture::result_type rloader_texture::operator()(entt::id_type id) const
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rloader_texture] loading: %x", id);
        std::vector<char> data;
        if(!rman().read_all_sync(id, data))
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "[rloader_texture] data loading failed: %x", id);
        SDL_Surface *surf;
        int w, h, chs;
        auto ptr = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()), data.size(), &w, &h, &chs, 4);
        assert(ptr);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rloader_texture] loaded: %dx%d, %dchs", w, h, chs);
        int pitch;
        pitch = w * chs;
        
        SDL_PixelFormat fmt;
        switch(chs)
        {
            case 4:
                fmt = SDL_PIXELFORMAT_ABGR8888;
                break;
            case 3:
                fmt = SDL_PIXELFORMAT_ABGR8888;
                break;
            default:
                assert(0);
        }
        SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, fmt, ptr, pitch);
        SDL_Surface* final = SDL_DuplicateSurface(surface);
        SDL_DestroySurface(surface);
        stbi_image_free(ptr);

        auto tex = std::make_shared<rtexture>();
        tex->surf = final;
        tex->tex = nullptr;
        tex->uploaded = false;
        return tex;
    }

    rloader_script::result_type rloader_script::operator()(entt::id_type id) const
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[rloader_script] loading: %x", id);
        auto script = std::make_shared<rscript>();
        script->valid = false;
        if(!rman().read_all_sync(id, script->raw))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[rloader_script] data loading failed: %x", id);
            return script;
        }
        
        // TODO allow for loading stored bytecode?
        script->type = script_type::LUA_SOURCE;
        script->valid = true;
        // TODO allow to use some sort of workqueue to parse source in parallel?
        

        return script;
    }
}
