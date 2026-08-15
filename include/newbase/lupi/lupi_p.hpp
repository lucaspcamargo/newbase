#pragma once

#include <newbase/lupi/lupi_lua.hpp>
#include <newbase/lupi/framebuffer.hpp>
#include <newbase/lupi/sprite.hpp>
#include <newbase/lupi/cart.hpp>
#include <newbase/lupi/input_state.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/sprite.hpp>
#include <entt/entity/entity.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nb {

struct lupi_p {
    lua_State* L { nullptr };
    bool running { false };

    lupi_framebuffer fb;
    lupi_palette     pal;
    lupi_gfx_state    gfx;

    lupi_button_state btn;
    lupi_mouse_state  mouse;
    lupi_text_queue   text;

    std::shared_ptr<rlupi_cart> cart;

    // Base resource-path directory the cart's manifest lives in (e.g.
    // "res/lupi_demo/lupi-balao-gatinho/"), trailing slash included. Used to
    // resolve sibling .lua requires, auto-discover img/**.png + maps/**.png
    // assets, and locate maps/*.json for the Tiled compiler.
    std::string cart_dir;

    // Every spritesheet discovered under cart_dir's img/+maps/ subtrees,
    // keyed by filename stem (matches the real cart's Sprites.find(name)
    // convention — see doc/system_lupi.md). Also the internal source of
    // truth ui.map()'s tileset lookups use directly, without a Lua round-trip.
    std::unordered_map<std::string, std::shared_ptr<lupi_spritesheet>> sprite_by_name;

    // scratch RGBA8888 buffer, reused every RENDER-phase blit
    std::vector<uint32_t> rgba_scratch;

    renderer_service::texture_handle tex { nullptr };

    // Wraps `tex` as an rtexture/rsprite so it can be shown via an ordinary
    // csprite entity in the default scene. render_simple-specific: it stores
    // `tex` directly in rtexture::tex (an SDL_Texture*) and marks it
    // pre-uploaded — render_gpu, which caches GPU textures separately keyed
    // by rtexture identity, isn't supported by this path. Built once, reused
    // across start()/stop() cycles.
    std::shared_ptr<rtexture> screen_tex;
    std::shared_ptr<rsprite>  screen_sprite;
    entt::entity screen_entity { entt::null };

    uint64_t frame_counter { 0 };

    // simple rolling stats for ui.stat()
    double last_step_ms { 0.0 };
    double cpu_ema      { 0.0 };
    double fps_ema       { 0.0 };

    // SDL_StartTextInput() needs a real SDL_Window*, which isn't available yet
    // at init() time in all cases — lazily started on the first event() call
    // once a keyboard-focused window exists (see lupi::event()).
    bool text_input_started { false };
};

}
