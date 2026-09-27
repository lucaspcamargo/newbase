#include <newbase/sys/textext/rtexfont.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>

#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>

#include <ryml.hpp>
#include <ryml_std.hpp>


using namespace nb;

bool rtexfont::do_load()
{
    log::info("[rtexfont] loading: %x", id());

    std::vector<char> data;
    if (!rman().read_all_sync(id(), data, true))
    {
        log::error("[rtexfont] cannot read: %x", id());
        return false;
    }

    auto tree = ryml::parse_in_place(c4::to_substr(data.data()));
    auto root = tree.rootref();

    if (!root.has_child("ttf") || !root.has_child("size"))
    {
        log::error("[rtexfont] missing required 'ttf' or 'size': %x", id());
        return false;
    }

    std::string ttf_path;
    root["ttf"] >> ttf_path;
    int font_size = 32;
    root["size"] >> font_size;

    std::string charset_str = "ascii";
    if (root.has_child("charset"))
        root["charset"] >> charset_str;

    // Build codepoint list
    std::vector<int> codepoints;
    if (charset_str == "digits")
    {
        for (const char* cc = " 0123456789.,:+-"; *cc; ++cc)
            codepoints.push_back((unsigned char)*cc);
    }
    else // ascii (default, also handles "ascii" and "digits_punct")
    {
        for (int i = 32; i <= 126; ++i)
            codepoints.push_back(i);
    }

    // Load TTF bytes
    auto ttf_id = entt::hashed_string{ttf_path.c_str()}.value();
    std::vector<char> ttf_data;
    if (!rman().read_all_sync(ttf_id, ttf_data))
    {
        log::error("[rtexfont] cannot read TTF '%s'", ttf_path.c_str());
        return false;
    }

    stbtt_fontinfo fi;
    if (!stbtt_InitFont(&fi, (const unsigned char*)ttf_data.data(), 0))
    {
        log::error("[rtexfont] stbtt_InitFont failed for '%s'", ttf_path.c_str());
        return false;
    }

    float scale = stbtt_ScaleForPixelHeight(&fi, (float)font_size);
    int asc_u, desc_u, lgap_u;
    stbtt_GetFontVMetrics(&fi, &asc_u, &desc_u, &lgap_u);

    auto ret       = std::make_shared<rtexfont>(id());
    font_size = font_size;
    ascent    = (int)(asc_u  * scale + 0.5f);
    descent   = (int)(desc_u * scale - 0.5f);
    line_gap  = (int)(lgap_u * scale + 0.5f);

    // Render bitmaps and compute atlas layout
    struct bake_entry {
        int cp, bw, bh, bx, by, advance;
        unsigned char* bmp; // nullptr for whitespace
    };
    std::vector<bake_entry> baked;
    baked.reserve(codepoints.size());

    const int MAX_W = 2048;
    int atlas_w = 0, atlas_h = 0, row_w = 0, row_h = 0;

    for (int cp : codepoints)
    {
        if (!stbtt_FindGlyphIndex(&fi, cp)) continue;

        int adv_u, lsb_u;
        stbtt_GetCodepointHMetrics(&fi, cp, &adv_u, &lsb_u);
        int advance = (int)(adv_u * scale + 0.5f);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&fi, cp, scale, scale, &x0, &y0, &x1, &y1);
        int bw = x1 - x0, bh = y1 - y0;

        bake_entry e { cp, bw, bh, x0, y0, advance, nullptr };
        if (bw > 0 && bh > 0)
        {
            e.bmp = stbtt_GetCodepointBitmap(&fi, scale, scale, cp, &bw, &bh, nullptr, nullptr);
            e.bw = bw; e.bh = bh;
            if (row_w + bw > MAX_W) { atlas_w = std::max(atlas_w, row_w); atlas_h += row_h + 1; row_w = 0; row_h = 0; }
            row_w += bw + 1;
            row_h  = std::max(row_h, bh);
        }
        baked.push_back(e);
    }
    atlas_w = std::max(atlas_w, row_w);
    atlas_h += row_h;

    // Round up to next power of two
    int pw = 1; while (pw < atlas_w) pw <<= 1;
    int ph = 1; while (ph < atlas_h) ph <<= 1;

    SDL_Surface* surf = SDL_CreateSurface(pw, ph, SDL_PIXELFORMAT_RGBA32);
    if (!surf)
    {
        log::error("[rtexfont] failed to create atlas surface: %s", SDL_GetError());
        for (auto& e : baked) if (e.bmp) stbtt_FreeBitmap(e.bmp, nullptr);
        return false;
    }
    SDL_ClearSurface(surf, 0.f, 0.f, 0.f, 0.f);

    // Second pass: blit into atlas and record glyphs
    int cx = 0, cy = 0;
    row_h = 0;
    auto* pixels = static_cast<Uint8*>(surf->pixels);
    int pitch = surf->pitch;

    for (auto& e : baked)
    {
        rtexfont::glyph g { 0, 0, 0, 0, e.bx, e.by, e.bw, e.bh, e.advance };

        if (e.bmp)
        {
            if (cx + e.bw > MAX_W) { cy += row_h + 1; cx = 0; row_h = 0; }

            for (int row = 0; row < e.bh; ++row)
                for (int col = 0; col < e.bw; ++col)
                {
                    Uint8* dst = pixels + (cy + row) * pitch + (cx + col) * 4;
                    dst[0] = dst[1] = dst[2] = 255;
                    dst[3] = e.bmp[row * e.bw + col];
                }

            g.u0 = (float)cx          / (float)pw;
            g.v0 = (float)cy          / (float)ph;
            g.u1 = (float)(cx + e.bw) / (float)pw;
            g.v1 = (float)(cy + e.bh) / (float)ph;

            cx   += e.bw + 1;
            row_h = std::max(row_h, e.bh);
            stbtt_FreeBitmap(e.bmp, nullptr);
        }
        glyphs[e.cp] = g;
    }

    auto atlas_tex          = std::make_shared<rtexture>(entt::hashed_string{ttf_path.c_str()}.value());
    atlas_tex->surf         = surf;
    atlas_tex->width        = surf->w;
    atlas_tex->height       = surf->h;
    atlas_tex->uploaded     = false;
    atlas_tex->rptr         = nullptr;
    atlas_tex->reload_surface = nullptr;
    atlas = atlas_tex;

    log::info("[rtexfont] '%s' size=%d glyphs=%zu atlas=%dx%d",
              ttf_path.c_str(), font_size, glyphs.size(), pw, ph);
    return true;
}
