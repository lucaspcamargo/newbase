#include <newbase/res/loaders.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/etree.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/sprite.hpp>
#include <newbase/res/script.hpp>
#include <newbase/res/vorbis.hpp>
#include <newbase/res/wav.hpp>
#include <newbase/res/yaml.hpp>
#include <newbase/res/particle_emitter.hpp>
#include <newbase/yaml/glm.hpp>
#include <newbase/log.hpp>
#include <ryml_std.hpp>

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

        std::vector<char> data;
        if (!rman().read_all_sync(id, data, true))
        {
            log::error("[rloader_sprite] cannot read: %x", id);
            return nullptr;
        }

        auto tree = ryml::parse_in_place(c4::to_substr(data.data()));
        auto root = tree.rootref();

        std::string tex_path;
        if (!root.has_child("texture"))
        {
            log::error("[rloader_sprite] missing 'texture' field: %x", id);
            return nullptr;
        }
        c4::from_chars(root["texture"].val(), &tex_path);

        auto ret = std::make_shared<rsprite>(id);

        // resolve and cache the texture
        auto tex_id = entt::hashed_string{tex_path.c_str()}.value();
        ret->tex = rman().get<rtexture>(tex_id);
        if (!ret->tex)
        {
            log::error("[rloader_sprite] cannot load texture '%s': %x", tex_path.c_str(), id);
            return nullptr;
        }

        if (root.has_child("anchor"))
        {
            ryml::ConstNodeRef an = root["anchor"];
            c4::from_chars(an[0].val(), &ret->anchor.x);
            c4::from_chars(an[1].val(), &ret->anchor.y);
        }

        if (root.has_child("dims"))
        {
            ryml::ConstNodeRef dm = root["dims"];
            c4::from_chars(dm[0].val(), &ret->dims.x);
            c4::from_chars(dm[1].val(), &ret->dims.y);
        }

        if (root.has_child("sequences"))
        {
            for (const auto& sn : root["sequences"])
            {
                sprite_sequence seq;
                if (sn.has_child("name"))     { std::string s; sn["name"] >> s; seq.name = s; }
                if (sn.has_child("loop"))     sn["loop"] >> seq.loop;
                if (sn.has_child("next"))     { std::string s; sn["next"] >> s; seq.next = s; }

                if (sn.has_child("strip"))
                {
                    const auto& st = sn["strip"];
                    float x = 0, y = 0, w = 0, h = 0, dur = 0.1f;
                    int count = 1;
                    if (st.has_child("x"))        st["x"]        >> x;
                    if (st.has_child("y"))        st["y"]        >> y;
                    if (st.has_child("w"))        st["w"]        >> w;
                    if (st.has_child("h"))        st["h"]        >> h;
                    if (st.has_child("count"))    st["count"]    >> count;
                    if (st.has_child("duration")) st["duration"] >> dur;
                    for (int i = 0; i < count; ++i)
                        seq.frames.push_back({ { x + w * i, y, w, h }, dur });
                }
                else if (sn.has_child("frames"))
                {
                    for (const auto& fn : sn["frames"])
                    {
                        sprite_frame fr;
                        if (fn.has_child("source_rect")) try_load_vec4(fn["source_rect"], fr.source_rect);
                        if (fn.has_child("duration"))    fn["duration"] >> fr.duration;
                        seq.frames.push_back(fr);
                    }
                }

                if (!seq.frames.empty())
                    ret->sequences.push_back(std::move(seq));
                else
                    log::warn("[rloader_sprite] sequence '%s' has no frames, skipped", seq.name.c_str());
            }
        }

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
        SDL_Surface* tmp  = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, ptr, w * 4);
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
        static constexpr float CACHE_THRESHOLD_SECS = 2.0f;

        log::info("[rloader_vorbis] loading: %x", id);
        auto vorbis = std::make_shared<rvorbis>(id);

        // Read the file into a temporary buffer for probing. We never store these
        // raw bytes in the resource; they are discarded after this function returns.
        std::vector<char> raw;
        if(!rman().read_all_sync(id, raw))
        {
            log::error("[rloader_vorbis] read failed: %x", id);
            return vorbis;
        }

        // Open a decoder just to read stream metadata.
        int err;
        stb_vorbis *v = stb_vorbis_open_memory(
            reinterpret_cast<const unsigned char*>(raw.data()), static_cast<int>(raw.size()),
            &err, nullptr);
        if(!v)
        {
            log::error("[rloader_vorbis] decode open failed (err %d): %x", err, id);
            return vorbis;
        }

        stb_vorbis_info info = stb_vorbis_get_info(v);
        int total = stb_vorbis_stream_length_in_samples(v);
        stb_vorbis_close(v);

        if(total < 0)
        {
            log::error("[rloader_vorbis] could not determine stream length: %x", id);
            return vorbis;
        }

        vorbis->spec = audio_spec{audio_format::S16,
                                  static_cast<uint8_t>(info.channels),
                                  static_cast<unsigned int>(info.sample_rate)};
        vorbis->total_frames = static_cast<std::size_t>(total);

        float duration_secs = static_cast<float>(total) / static_cast<float>(info.sample_rate);

        if(duration_secs <= CACHE_THRESHOLD_SECS)
        {
            // Short sample: decode fully and cache the PCM.
            short *pcm = nullptr;
            int num_ch, freq;
            int n = stb_vorbis_decode_memory(
                reinterpret_cast<const unsigned char*>(raw.data()), static_cast<int>(raw.size()),
                &num_ch, &freq, &pcm);
            if(n > 0 && pcm)
            {
                const std::size_t size_bytes = sizeof(short) * static_cast<std::size_t>(n) * static_cast<std::size_t>(num_ch);
                vorbis->frames.resize(size_bytes);
                memcpy(vorbis->frames.data(), pcm, size_bytes);
                free(pcm);
                vorbis->cached = true;
                log::info("[rloader_vorbis] cached: %x, %d frames, %.2fs, %d Hz, %d ch",
                          id, n, duration_secs, freq, num_ch);
            }
            else
            {
                log::error("[rloader_vorbis] decode failed for short sample: %x", id);
                return vorbis;
            }
        }
        else
        {
            // Long file: store only metadata; producer streams from storage.
            log::info("[rloader_vorbis] streaming: %x, %d frames, %.2fs, %d Hz, %d ch",
                      id, total, duration_secs, info.sample_rate, info.channels);
        }

        vorbis->valid = true;
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

    // particle emitter loader
    rloader_particle_emitter::result_type rloader_particle_emitter::operator()(entt::id_type id) const
    {
        log::info("[rloader_particle_emitter] loading: %x", id);

        std::vector<char> data;
        if (!rman().read_all_sync(id, data, true))
        {
            log::error("[rloader_particle_emitter] cannot read: %x", id);
            return nullptr;
        }

        auto tree = ryml::parse_in_place(c4::to_substr(data.data()));
        auto root = tree.rootref();
        auto ret  = std::make_shared<rparticle_emitter>(id);

        if (root.has_child("max_particles"))
        {
            int mp = ret->max_particles;
            root["max_particles"] >> mp;
            ret->max_particles = mp;
        }

        if (root.has_child("pre_simulate_frames"))
        {
            int psf = 0;
            root["pre_simulate_frames"] >> psf;
            ret->pre_simulate_frames = psf;
        }

        if (root.has_child("initial_burst"))
        {
            int ib = 0;
            root["initial_burst"] >> ib;
            ret->initial_burst = ib;
        }

        if (root.has_child("blend_mode"))
        {
            std::string bm;
            root["blend_mode"] >> bm;
            if (bm == "add") ret->blend_mode = particle_blend_mode::ADD;
        }

        if (root.has_child("texture"))
        {
            std::string tex_path;
            root["texture"] >> tex_path;
            auto tex_id = entt::hashed_string{tex_path.c_str()}.value();
            ret->tex = rman().get<rtexture>(tex_id);
            if (!ret->tex)
                log::warn("[rloader_particle_emitter] cannot load texture '%s'", tex_path.c_str());
        }

        if (root.has_child("emitter"))
        {
            auto em  = root["emitter"];
            auto& cfg = ret->emitter;
            if (em.has_child("rate"))               try_load_float(em["rate"],               cfg.rate);
            if (em.has_child("lifetime"))           try_load_float(em["lifetime"],            cfg.lifetime);
            if (em.has_child("lifetime_variance"))  try_load_float(em["lifetime_variance"],   cfg.lifetime_variance);
            if (em.has_child("pos_variance"))       try_load_vec2 (em["pos_variance"],        cfg.pos_variance);
            if (em.has_child("vel"))                try_load_vec2 (em["vel"],                 cfg.vel);
            if (em.has_child("vel_variance"))       try_load_vec2 (em["vel_variance"],        cfg.vel_variance);
            if (em.has_child("vel_angle_variance")) try_load_float(em["vel_angle_variance"],  cfg.vel_angle_variance);
            if (em.has_child("color"))              try_load_vec4 (em["color"],               cfg.color);
            if (em.has_child("color_variance"))     try_load_vec4 (em["color_variance"],      cfg.color_variance);
            if (em.has_child("scale"))              try_load_float(em["scale"],               cfg.scale);
            if (em.has_child("scale_variance"))     try_load_float(em["scale_variance"],      cfg.scale_variance);
            if (em.has_child("rotation"))           try_load_float(em["rotation"],            cfg.rotation);
            if (em.has_child("rotation_variance"))  try_load_float(em["rotation_variance"],   cfg.rotation_variance);
        }

        if (root.has_child("affectors"))
        {
            for (auto aff_node : root["affectors"])
            {
                particle_affector_config aff;

                std::string type_str;
                if (aff_node.has_child("type")) aff_node["type"] >> type_str;

                using AT = particle_affector_config::type;
                if      (type_str == "acceleration") aff.affector_type = AT::ACCELERATION;
                else if (type_str == "drag")         aff.affector_type = AT::DRAG;
                else if (type_str == "color_fade")   aff.affector_type = AT::COLOR_FADE;
                else if (type_str == "scale_fade")   aff.affector_type = AT::SCALE_FADE;
                else if (type_str == "attractor")    aff.affector_type = AT::ATTRACTOR;
                else
                {
                    log::warn("[rloader_particle_emitter] unknown affector type '%s'", type_str.c_str());
                    continue;
                }

                if (aff_node.has_child("value"))        try_load_vec2 (aff_node["value"],        aff.vec2_val);
                if (aff_node.has_child("rgba_per_sec")) try_load_vec4 (aff_node["rgba_per_sec"], aff.vec4_val);
                if (aff_node.has_child("per_sec"))      try_load_float(aff_node["per_sec"],       aff.float_val);
                if (aff_node.has_child("gain"))         try_load_float(aff_node["gain"],          aff.float_val);
                if (aff_node.has_child("kill_on_zero")) try_load_bool (aff_node["kill_on_zero"],  aff.kill_on_zero);

                ret->affectors.push_back(aff);
            }
        }

        return ret;
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
