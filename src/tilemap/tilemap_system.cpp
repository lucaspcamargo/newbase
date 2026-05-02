#include <newbase/tilemap/tilemap_system.hpp>
#include <newbase/components/tilemap.hpp>
#include <newbase/components/mesh2d.hpp>
#include <newbase/components/body2d.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/log.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/res/manager.hpp>
#include <entt/entt.hpp>
#include <entt/meta/factory.hpp>
#include <utility>

using namespace nb;
using entt::operator""_hs;

tilemap_system::tilemap_system()  = default;
tilemap_system::~tilemap_system() = default;

bool tilemap_system::init(ryml::ConstNodeRef /*cfg*/) { return true; }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void build_tile_mesh(const rtilemap& map, cmesh2d& mesh, const std::string& render_layer)
{
    if (map.tilesets.empty()) return;
    const tilemap_tileset& ts = map.tilesets[0];
    if (!ts.tex || ts.columns <= 0 || ts.tile_width <= 0 || ts.tile_height <= 0) return;

    const float atlas_w = static_cast<float>(ts.image_width  > 0 ? ts.image_width
        : ts.margin * 2 + ts.columns * ts.tile_width + (ts.columns - 1) * ts.spacing);
    const int rows = (ts.tilecount + ts.columns - 1) / ts.columns;
    const float atlas_h = static_cast<float>(ts.image_height > 0 ? ts.image_height
        : ts.margin * 2 + rows * ts.tile_height + (rows - 1) * ts.spacing);
    if (atlas_w <= 0.f || atlas_h <= 0.f) return;

    auto geom = std::make_shared<geometry_buffer_2d>();

    for (const auto& layer : map.layers)
    {
        if (layer.layer_type != tilemap_layer::type::TILE) continue;
        if (!layer.visible) continue;
        if (!render_layer.empty() && layer.name != render_layer) continue;

        const glm::vec4 color { 1.f, 1.f, 1.f, layer.opacity };

        for (int row = 0; row < layer.height; ++row)
        for (int col = 0; col < layer.width;  ++col)
        {
            const int idx = row * layer.width + col;
            if (idx >= static_cast<int>(layer.tiles.size())) continue;

            const int raw_gid = layer.tiles[idx];
            if (raw_gid == 0) continue;

            // Strip Tiled flip flags from the upper three bits
            const bool flip_h = (raw_gid & 0x80000000) != 0;
            const bool flip_v = (raw_gid & 0x40000000) != 0;
            const bool flip_d = (raw_gid & 0x20000000) != 0;
            const int gid = raw_gid & 0x1FFFFFFF;

            const tilemap_tileset* tileset = map.find_tileset(gid);
            if (!tileset) continue;

            const int local_id = gid - tileset->firstgid;
            const int tile_col = local_id % tileset->columns;
            const int tile_row = local_id / tileset->columns;

            const float tw = static_cast<float>(tileset->tile_width);
            const float th = static_cast<float>(tileset->tile_height);
            const float ox = static_cast<float>(tileset->margin + tile_col * (tileset->tile_width  + tileset->spacing));
            const float oy = static_cast<float>(tileset->margin + tile_row * (tileset->tile_height + tileset->spacing));
            const float u0 = ox / atlas_w;
            const float v0 = oy / atlas_h;
            const float u1 = (ox + tw) / atlas_w;
            const float v1 = (oy + th) / atlas_h;

            // Apply Tiled flip flags to UV corners (order: diagonal → horizontal → vertical)
            glm::vec2 uv_tl{u0, v0}, uv_tr{u1, v0}, uv_bl{u0, v1}, uv_br{u1, v1};
            if (flip_d) std::swap(uv_tr, uv_bl);
            if (flip_h) { std::swap(uv_tl, uv_tr); std::swap(uv_bl, uv_br); }
            if (flip_v) { std::swap(uv_tl, uv_bl); std::swap(uv_tr, uv_br); }

            const float px = static_cast<float>(col) * tw;
            const float py = static_cast<float>(row) * th;

            geom->push_quad(
                {{ px,      py      }, uv_tl, color },
                {{ px + tw, py      }, uv_tr, color },
                {{ px,      py + th }, uv_bl, color },
                {{ px + tw, py + th }, uv_br, color }
            );
        }
    }

    mesh.geom       = std::move(geom);
    mesh.tex        = ts.tex;
    mesh.blend_mode = blend_mode_2d::ALPHA;
    mesh.visible    = true;
}

// Apply Tiled flip flags to a point in tile-local coordinates.
// Order matches the UV transform: diagonal first, then horizontal, then vertical.
// For non-square tiles, flip_d reflects across the normalised anti-diagonal.
static glm::vec2 transform_tile_point(glm::vec2 p, float tw, float th,
                                       bool flip_h, bool flip_v, bool flip_d)
{
    if (flip_d)
    {
        // Reflect across the anti-diagonal in normalised space: (u,v) → (v,u).
        const float u = p.x / tw, v = p.y / th;
        p = { v * tw, u * th };
    }
    if (flip_h) p.x = tw - p.x;
    if (flip_v) p.y = th - p.y;
    return p;
}

static void build_collision_body(const rtilemap& map, const std::string& layer_name, cbody2d& body)
{
    const tilemap_layer* layer = nullptr;
    for (const auto& l : map.layers)
        if (l.name == layer_name) { layer = &l; break; }

    if (!layer || layer->layer_type != tilemap_layer::type::TILE)
    {
        log::warn("[tilemap_system] collision layer '%s' not found or not a tile layer",
                  layer_name.c_str());
        return;
    }

    const float tw = static_cast<float>(map.tile_width);
    const float th = static_cast<float>(map.tile_height);

    body.type         = body2d_type::STATIC;
    body.fix_rotation = true;
    body.dirty        = true;

    // Returns the tileset and local tile ID for a grid cell, or nullptr if empty.
    auto tile_at = [&](int row, int col) -> std::pair<const tilemap_tileset*, int>
    {
        const int idx = row * layer->width + col;
        if (idx >= static_cast<int>(layer->tiles.size())) return {nullptr, 0};
        const int raw_gid = layer->tiles[idx];
        if ((raw_gid & 0x1FFFFFFF) == 0) return {nullptr, 0};
        const int gid = raw_gid & 0x1FFFFFFF;
        const tilemap_tileset* ts = map.find_tileset(gid);
        if (!ts) return {nullptr, 0};
        return {ts, gid - ts->firstgid};
    };

    // Pass 1: emit custom-shaped tiles (those with a tile_shapes objectgroup entry).
    for (int row = 0; row < layer->height; ++row)
    for (int col = 0; col < layer->width;  ++col)
    {
        const int idx = row * layer->width + col;
        if (idx >= static_cast<int>(layer->tiles.size())) continue;
        const int raw_gid = layer->tiles[idx];
        if (raw_gid == 0) continue;

        const bool flip_h = (raw_gid & 0x80000000) != 0;
        const bool flip_v = (raw_gid & 0x40000000) != 0;
        const bool flip_d = (raw_gid & 0x20000000) != 0;

        auto [ts, local_id] = tile_at(row, col);
        if (!ts) continue;

        auto it = ts->tile_shapes.find(local_id);
        if (it == ts->tile_shapes.end()) continue; // solid by default — handled in pass 2
        // Empty outer vector = objectgroup with no shapes = passthrough, skip.
        if (it->second.empty()) continue;

        const float px = col * tw;
        const float py = row * th;

        for (const auto& poly : it->second)
        {
            shape2d s;
            s.shape_type = shape2d_type::POLY;
            s.shape_data.reserve(poly.size() * 2);
            for (const glm::vec2& pt : poly)
            {
                const glm::vec2 tp = transform_tile_point(pt, tw, th, flip_h, flip_v, flip_d);
                s.shape_data.push_back(px + tp.x);
                s.shape_data.push_back(py + tp.y);
            }
            body.shapes.push_back(std::move(s));
        }
    }

    // Pass 2: run-merge fully solid tiles (no tile_shapes entry at all).
    // Tiles with a tile_shapes entry (even empty) are excluded — they were either
    // handled above or are intentionally passthrough.
    auto solid = [&](int row, int col) -> bool
    {
        auto [ts, local_id] = tile_at(row, col);
        if (!ts) return false;
        return ts->tile_shapes.find(local_id) == ts->tile_shapes.end();
    };

    for (int row = 0; row < layer->height; ++row)
    {
        int col = 0;
        while (col < layer->width)
        {
            if (!solid(row, col)) { ++col; continue; }

            const int run_start = col;
            while (col < layer->width && solid(row, col)) ++col;

            const float x0 = run_start * tw;
            const float x1 = col       * tw;
            const float y0 = row       * th;
            const float y1 = (row + 1) * th;

            shape2d s;
            s.shape_type = shape2d_type::POLY;
            s.shape_data = { x0, y0, x1, y0, x1, y1, x0, y1 };
            body.shapes.push_back(std::move(s));
        }
    }

    log::info("[tilemap_system] built %zu collision shapes from layer '%s'",
              body.shapes.size(), layer_name.c_str());
}

// ---------------------------------------------------------------------------
// Step
// ---------------------------------------------------------------------------

bool tilemap_system::step(step_phase phase)
{
    if (phase != step_phase::PRE_UPDATE) return true;

    auto& reg = engine::instance().default_scene().registry();
    auto  view = reg.view<ctilemap>();

    for (auto [eid, tm] : view.each())
    {
        if (tm._built) continue;
        if (!tm.map)   continue;

        log::info("[tilemap_system] building entity %u", static_cast<uint32_t>(eid));

        // Visual mesh
        auto& mesh = reg.get_or_emplace<cmesh2d>(eid);
        build_tile_mesh(*tm.map, mesh, tm.render_layer);
        mesh.visible = tm.visible;
        mesh.pixel_snap = true;

        // Collision body
        if (!tm.collision_layer.empty())
        {
            auto& body = reg.get_or_emplace<cbody2d>(eid);
            build_collision_body(*tm.map, tm.collision_layer, body);
        }

        tm._built = true;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Object layer queries
// ---------------------------------------------------------------------------

static const nb::tilemap_layer* find_layer(const nb::rtilemap* map, const std::string& name)
{
    if (!map) return nullptr;
    for (const auto& layer : map->layers)
        if (layer.layer_type == nb::tilemap_layer::type::OBJECT && layer.name == name)
            return &layer;
    return nullptr;
}

unsigned int nb::tilemap_system::get_layer_object_count(entt::id_type map_id, std::string layer_name) const
{
    auto res = nb::rman().get<nb::rtilemap>(map_id);
    const auto* layer = find_layer(res.get(), layer_name);
    return layer ? static_cast<unsigned int>(layer->objects.size()) : 0u;
}

nb::tilemap_object nb::tilemap_system::get_layer_object(entt::id_type map_id, std::string layer_name, unsigned int idx) const
{
    auto res = nb::rman().get<nb::rtilemap>(map_id);
    const auto* layer = find_layer(res.get(), layer_name);
    if (!layer || idx >= layer->objects.size()) return {};
    return layer->objects[idx];
}

// ---------------------------------------------------------------------------
// RTTI
// ---------------------------------------------------------------------------

extern "C" void _rtti_init_tilemap_system()
{
    entt::meta_factory<nb::tilemap_object>{}
        .type("tilemap_object"_hs)
        .ctor<>()
        .data<&nb::tilemap_object::id>("id"_hs)
            .custom<rtti::data_info>(rtti::data_info{"id"})
        .data<&nb::tilemap_object::name>("name"_hs)
            .custom<rtti::data_info>(rtti::data_info{"name"})
        .data<&nb::tilemap_object::type>("type"_hs)
            .custom<rtti::data_info>(rtti::data_info{"type"})
        .data<&nb::tilemap_object::shape>("shape"_hs)
            .custom<rtti::data_info>(rtti::data_info{"shape"})
        .data<&nb::tilemap_object::x>("x"_hs)
            .custom<rtti::data_info>(rtti::data_info{"x"})
        .data<&nb::tilemap_object::y>("y"_hs)
            .custom<rtti::data_info>(rtti::data_info{"y"})
        .data<&nb::tilemap_object::width>("width"_hs)
            .custom<rtti::data_info>(rtti::data_info{"width"})
        .data<&nb::tilemap_object::height>("height"_hs)
            .custom<rtti::data_info>(rtti::data_info{"height"})
        .data<&nb::tilemap_object::gid>("gid"_hs)
            .custom<rtti::data_info>(rtti::data_info{"gid"});

    entt::meta_factory<nb::tilemap_system>{}
        .type("tilemap_system"_hs)
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "tilemap_system",
            .type_class = rtti::TYPE_CLASS_SYSTEM
        })
        .base<nb::system>()
        .func<&nb::tilemap_system::get_layer_object_count>("get_layer_object_count"_hs)
            .custom<rtti::func_info>(rtti::func_info{"get_layer_object_count"})
        .func<&nb::tilemap_system::get_layer_object>("get_layer_object"_hs)
            .custom<rtti::func_info>(rtti::func_info{"get_layer_object"});

    entt::meta_factory<std::shared_ptr<nb::tilemap_system>>{rtti::ctx_systems()}
        .type("tilemap_system_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::tilemap_system>>()
        .conv<std::shared_ptr<nb::system>>();

    ctilemap::_ensure_rtti();
}
