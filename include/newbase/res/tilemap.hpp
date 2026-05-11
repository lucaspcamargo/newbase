#pragma once

#include <newbase/res/resource.hpp>
#include <newbase/res/texture.hpp>
#include <entt/entt.hpp>
#include <glm/vec2.hpp>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
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

    // Per-tile collision shapes from Tiled's tile collision editor.
    // Key: local tile ID (gid - firstgid).
    // Value: list of convex polygons, each a flat list of (x,y) pairs in tile-local coords.
    // Key present, empty outer vector → tile has an objectgroup with no shapes (passthrough).
    // Key absent → no objectgroup → tile is solid by default (full rectangle).
    std::unordered_map<int, std::vector<std::vector<glm::vec2>>> tile_shapes;

    // Per-tile custom properties from Tiled (tile Properties panel).
    // Key: local tile ID. Value: property name → value as meta_any.
    // Types: bool, int, float, std::string (string/color/file/class).
    std::unordered_map<int, std::unordered_map<std::string, entt::meta_any>> tile_properties;
};

struct tilemap_object {
    enum shape_type : int { SHAPE_POINT = 0, SHAPE_RECTANGLE = 1, SHAPE_TILE = 2 };

    int         id     { 0 };
    std::string name;
    std::string type;   // Tiled "class" field (or legacy "type")
    int         shape  { SHAPE_POINT };
    float       x      { 0.f };
    float       y      { 0.f };
    float       width  { 0.f };
    float       height { 0.f };
    int         gid    { 0 };   // tile objects only
    std::unordered_map<std::string, std::string> properties;
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
    std::vector<tilemap_object> objects; // only for OBJECT layers
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
