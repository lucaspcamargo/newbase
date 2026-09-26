#include <newbase/res/sprite.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/yaml/glm.hpp>
#include <newbase/log.hpp>

#include <ryml.hpp>
#include <ryml_std.hpp>

using namespace nb;

bool rsprite::do_load()
{
    log::info("[rsprite] loading: %x", id());

    std::vector<char> data;
    if (!rman().read_all_sync(id(), data, true))
    {
        log::error("[rsprite] cannot read: %x", id());
        return false;
    }

    auto tree = ryml::parse_in_place(c4::to_substr(data.data()));
    auto root = tree.rootref();

    auto ret = std::make_shared<rsprite>(id());

    std::string tex_path;
    if (root.has_child("texture"))
    {
        c4::from_chars(root["texture"].val(), &tex_path);

        // resolve and cache the texture
        auto tex_id = entt::hashed_string{tex_path.c_str()}.value();
        tex = rman().load_sync<rtexture>(tex_id);
        if (!tex)
        {
            log::error("[rsprite] cannot load texture '%s': %x", tex_path.c_str(), id());
            return false;
        }
    }
    else
    {
        log::warn("[rsprite] missing 'texture' field: %x", id());
    }

    if (root.has_child("anchor"))
    {
        ryml::ConstNodeRef an = root["anchor"];
        c4::from_chars(an[0].val(), &anchor.x);
        c4::from_chars(an[1].val(), &anchor.y);
    }

    if (root.has_child("dims"))
    {
        ryml::ConstNodeRef dm = root["dims"];
        c4::from_chars(dm[0].val(), &dims.x);
        c4::from_chars(dm[1].val(), &dims.y);
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
                sequences.push_back(std::move(seq));
            else
                log::warn("[rloader_sprite] sequence '%s' has no frames, skipped", seq.name.c_str());
        }
    }

    return true;
}
