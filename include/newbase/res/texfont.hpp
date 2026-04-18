#pragma once

#include <newbase/res/resource.hpp>
#include <newbase/res/texture.hpp>
#include <unordered_map>
#include <memory>

namespace nb {

struct rtexfont : resource
{
    struct glyph {
        float u0, v0, u1, v1;  // atlas UV coords
        int   bx, by;           // bearing (offset from cursor to glyph top-left)
        int   bw, bh;           // bitmap dimensions
        int   advance;          // horizontal advance
    };

    explicit rtexfont(entt::id_type id) : resource(id, entt::hashed_string{"rtexfont"}.value()) {}

    std::shared_ptr<rtexture>         atlas;
    std::unordered_map<int, glyph>    glyphs;
    int ascent  { 0 };
    int descent { 0 };
    int line_gap{ 0 };
    int font_size { 0 };

    bool has_glyph(int cp) const { return glyphs.count(cp) > 0; }
    const glyph* get_glyph(int cp) const
    {
        auto it = glyphs.find(cp);
        return it != glyphs.end() ? &it->second : nullptr;
    }
};

}
