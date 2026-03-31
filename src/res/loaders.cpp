#include <newbase/res/loaders.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/etree.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/sprite.hpp>
#include <newbase/res/script.hpp>
#include <newbase/res/vorbis.hpp>
#include <newbase/res/wav.hpp>
#include <newbase/res/yaml.hpp>
#include <newbase/log.hpp>

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
        auto ret = std::make_shared<retree>(id);
        auto read_success = rman().read_all_sync(id, ret->data, true);
        if(read_success)
        {
            ret->tree = ryml::parse_in_place(c4::to_substr(ret->data.data()));
            ret->yaml_valid = ret->tree.rootref().is_seq();
            ret->etree_valid = ret->yaml_valid; // TODO parse to specific etree data
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
        auto ret = std::make_shared<rsprite>(id);
        ret->id_tex = id;
        return ret;
    }

    // Standalone surface loader: reads asset bytes via the resource manager and decodes
    // them with stb_image.  Returns a caller-owned SDL_Surface*, or nullptr on failure.
    static SDL_Surface* load_texture_surface(entt::id_type id)
    {
        std::vector<char> data;
        if (!rman().read_all_sync(id, data))
        {
            log::error("[load_texture_surface] data loading failed: %x", id);
            return nullptr;
        }
        int w, h, chs;
        auto ptr = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                         static_cast<int>(data.size()), &w, &h, &chs, 4);
        if (!ptr)
        {
            log::error("[load_texture_surface] stbi decode failed: %x", id);
            return nullptr;
        }
        log::info("[load_texture_surface] loaded: %dx%d, %dchs", w, h, chs);
        SDL_Surface* tmp  = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, ptr, w * 4);
        SDL_Surface* surf = SDL_DuplicateSurface(tmp);
        SDL_DestroySurface(tmp);
        stbi_image_free(ptr);
        return surf;
    }

    // texture loader
    rloader_texture::result_type rloader_texture::operator()(entt::id_type id) const
    {
        log::info("[rloader_texture] loading: %x", id);
        auto tex = std::make_shared<rtexture>(id);
        tex->surf           = load_texture_surface(id);
        tex->tex            = nullptr;
        tex->uploaded       = false;
        tex->reload_surface = load_texture_surface;
        return tex;
    }

    rloader_script::result_type rloader_script::operator()(entt::id_type id) const
    {
        log::info("[rloader_script] loading: %x", id);
        auto script = std::make_shared<rscript>(id);
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

        auto &handles = rman().handles();
        auto it = handles.find(id);
        if (it != handles.end() && !it->second.path.empty())
        
            script->chunkname = it->second.path;
        else
        {    
            char buf[18];
            snprintf(buf, sizeof(buf), "%x", id);
            script->chunkname = buf;
        }

        return script;
    }

    // vorbis loader
    rloader_vorbis::result_type rloader_vorbis::operator()(entt::id_type id) const
    {
        log::info("[rloader_vorbis] loading: %x", id);
        auto vorbis = std::make_shared<rvorbis>(id);
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
            vorbis->spec = audio_spec{audio_format::S16, num_ch, static_cast<unsigned int>(freq)};
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

    rloader_wav::result_type rloader_wav::operator()(entt::id_type id) const
    {
        log::info("[rloader_wav] loading: %x", id);
        auto wav = std::make_shared<rwav>(id);
        wav->valid = false;
        std::vector<char> data;
        if(!rman().read_all_sync(id, data))
        {
            log::error("[rloader_wav] data loading failed: %x", id);
            return wav;
        }

        auto ios = SDL_IOFromConstMem(data.data(), data.size());
        if(!ios)
        {
            log::error("[rloader_wav] io create failed: %x", id);
            return wav;
        }
        
        SDL_AudioSpec spec;

        if(SDL_LoadWAV_IO(ios, true, &spec, &wav->buf, &wav->len))
        {
            wav->spec.from_sdl(spec);
            wav->valid = wav->spec.format != audio_format::UNKNOWN;
        }
        else
        {
            log::error("[rloader_wav] decode failed: %x: %s", id, SDL_GetError());
            return wav;
        }

        return wav;
    }

    rwav::~rwav() 
    {
        if(buf)
        {
            SDL_free(buf);
        }
    }

    // generic yaml resource loader
    rloader_yaml::result_type rloader_yaml::operator()(entt::id_type id) const
    {
        log::info("[rloader_yaml] loading: %x", id);
        auto ret = std::make_shared<ryaml>(id);
        auto read_success = rman().read_all_sync(id, ret->data, true);
        if(read_success)
        {
            ret->tree = ryml::parse_in_place(c4::to_substr(ret->data.data()));
            ret->yaml_valid = true; 
        }
        else
        {
            log::error("[rloader_yaml] cannot read: %x", id);
            return nullptr; // let go of allocated resource, fail to load
        }
        return ret;
    }
}
