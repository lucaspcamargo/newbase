#include <newbase/res/loaders.h>
#include <newbase/res/manager.h>
#include <newbase/res/etree.h>
#include <newbase/res/texture.h>
#include <newbase/res/sprite.h>
#include <newbase/res/script.h>
#include <newbase/res/vorbis.h>
#include <newbase/log.h>

#include <SDL3/SDL.h>
#include <stb_image.h>
#include <lua.h>
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <memory>


namespace nb {

    // etree loader
    rloader_etree::result_type rloader_etree::operator()(entt::id_type id) const
    {
        log::info("[rloader_etree] loading: %x", id);
        auto ret = std::make_shared<retree>();
        auto read_success = rman().read_all_sync(id, ret->data, true);
        if(read_success)
        {
            ret->tree = ryml::parse_in_place(c4::to_substr(ret->data.data()));
            ret->valid = ret->tree.rootref().is_seq();
        }
        else
        {
            log::error("[rloader_etree] cannot read: %x", id);
            return nullptr; // let go of allocated resource, fail to load
        }
        return ret;
    }

    // sprite loader
    rloader_sprite::result_type rloader_sprite::operator()(entt::id_type id) const
    {
        log::info("[rloader_sprite] loading: %x", id);
        // TODO have sprite data file?
        // for now just load texture
        auto ret = std::make_shared<rsprite>();
        ret->id_tex = id;
        return ret;
    }

    // texture loader
    rloader_texture::result_type rloader_texture::operator()(entt::id_type id) const
    {
        log::info("[rloader_texture] loading: %x", id);
        std::vector<char> data;
        if(!rman().read_all_sync(id, data))
            log::error("[rloader_texture] data loading failed: %x", id);
        SDL_Surface *surf;
        int w, h, chs;
        auto ptr = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()), data.size(), &w, &h, &chs, 4);
        assert(ptr);
        log::info("[rloader_texture] loaded: %dx%d, %dchs", w, h, chs);
        int pitch;
        pitch = w * 4;
        
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
        log::info("[rloader_script] loading: %x", id);
        auto script = std::make_shared<rscript>();
        script->valid = false;
        if(!rman().read_all_sync(id, script->raw))
        {
            log::error("[rloader_script] data loading failed: %x", id);
            return script;
        }
        
        // TODO allow for loading stored bytecode?
        script->type = script_type::LUA_SOURCE;
        script->valid = true;
        // TODO allow to use some sort of workqueue to parse source in parallel?
        

        return script;
    }

    rloader_vorbis::result_type rloader_vorbis::operator()(entt::id_type id) const
    {
        log::info("[rloader_vorbis] loading: %x", id);
        auto vorbis = std::make_shared<rvorbis>();
        vorbis->valid = false;
        if(!rman().read_all_sync(id, vorbis->data))
        {
            log::error("[rloader_vorbis] data loading failed: %x", id);
            return vorbis;
        }
        
        short *result;
        int num_ch;
        int freq;
        int samplecount = stb_vorbis_decode_memory((const uint8_t*)vorbis->data.data(), vorbis->data.size(), &num_ch, &freq, &result);
        
        if(samplecount >= 0 && result)
        {
            // success
            const size_t size_bytes = sizeof(short)*samplecount*num_ch;
            vorbis->frames.resize(size_bytes);
            memcpy(vorbis->frames.data(), result, size_bytes);
            free(result);
            // NOTE: we always get interleaved, 16-bit signed audio from stb_vorbis
            vorbis->spec = SDL_AudioSpec{SDL_AUDIO_S16, num_ch, freq};
            vorbis->decoded = true;
            vorbis->valid = true;
            log::info("[rloader_vorbis] loaded: %x, %d frames at %d Hz, %d channels: %d bytes", id, samplecount, freq, num_ch, vorbis->frames.size());
        }
        else
        {
            log::error("[rloader_vorbis] data decode failed: %x", id);
        }

        return vorbis;
    }
}
