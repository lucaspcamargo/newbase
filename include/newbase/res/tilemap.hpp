#pragma once

#include <newbase/res/resource.hpp>
#include <newbase/res/texture.hpp>
#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <vector>

namespace nb {

struct tilemap_tileset {
    int firstgid     { 1 };
    int columns      { 0 };
    int tilecount    { 0 };
    int tile_width   { 0 };
    int tile_height  { 0 };
    int spacing      { 0 }; // pixels between tiles
    int margin       { 0 }; // pixels around the image border
    int image_width  { 0 };
    int image_height { 0 };
    std::shared_ptr<rtexture> tex;
};

struct tilemap_layer {
    enum class type { TILE, OBJECT };

    std::string      name;
    int              width  { 0 };
    int              height { 0 };
    std::vector<int> tiles; // GIDs, row-major, 0 = empty; only for TILE layers
    bool             visible { true };
    float            opacity { 1.f };
    type             layer_type { type::TILE };
};

struct rtilemap : public resource {
    explicit rtilemap(entt::id_type id = 0)
        : resource(id, entt::hashed_string{"rtilemap"}.value()) {}

    int width       { 0 }; // map width in tiles
    int height      { 0 }; // map height in tiles
    int tile_width  { 0 }; // pixels per tile
    int tile_height { 0 };

    std::vector<tilemap_tileset> tilesets;
    std::vector<tilemap_layer>   layers;

    // Returns the tileset that owns the given GID (after stripping flip flags).
    const tilemap_tileset* find_tileset(int gid) const
    {
        const tilemap_tileset* best = nullptr;
        for (const auto& ts : tilesets)
            if (gid >= ts.firstgid && (!best || ts.firstgid > best->firstgid))
                best = &ts;
        return best;
    }
};

} // namespace nb
