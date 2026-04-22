#include <newbase/tilemap/tilemap_system.hpp>
#include <newbase/components/tilemap.hpp>
#include <newbase/components/mesh2d.hpp>
#include <newbase/components/body2d.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/log.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <entt/entt.hpp>
#include <entt/meta/factory.hpp>

using namespace nb;
using entt::operator""_hs;

tilemap_system::tilemap_system()  = default;
tilemap_system::~tilemap_system() = default;

bool tilemap_system::init(ryml::ConstNodeRef /*cfg*/) { return true; }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void build_tile_mesh(const rtilemap& map, cmesh2d& mesh)
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

        const glm::vec4 color { 1.f, 1.f, 1.f, layer.opacity };

        for (int row = 0; row < layer.height; ++row)
        for (int col = 0; col < layer.width;  ++col)
        {
            const int idx = row * layer.width + col;
            if (idx >= static_cast<int>(layer.tiles.size())) continue;

            const int raw_gid = layer.tiles[idx];
            if (raw_gid == 0) continue;

            // Strip Tiled flip flags from the upper three bits
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

            const float px = static_cast<float>(col) * tw;
            const float py = static_cast<float>(row) * th;

            geom->push_quad(
                {{ px,      py      }, { u0, v0 }, color },
                {{ px + tw, py      }, { u1, v0 }, color },
                {{ px,      py + th }, { u0, v1 }, color },
                {{ px + tw, py + th }, { u1, v1 }, color }
            );
        }
    }

    mesh.geom       = std::move(geom);
    mesh.tex        = ts.tex;
    mesh.blend_mode = blend_mode_2d::ALPHA;
    mesh.visible    = true;
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

    auto solid = [&](int row, int col) -> bool {
        const int idx = row * layer->width + col;
        if (idx >= static_cast<int>(layer->tiles.size())) return false;
        return (layer->tiles[idx] & 0x1FFFFFFF) != 0;
    };

    // Merge consecutive solid tiles in each row into a single wide shape.
    for (int row = 0; row < layer->height; ++row)
    {
        int col = 0;
        while (col < layer->width)
        {
            if (!solid(row, col)) { ++col; continue; }

            int run_start = col;
            while (col < layer->width && solid(row, col)) ++col;
            int run_end = col; // exclusive

            const float x0 = run_start * tw;
            const float x1 = run_end   * tw;
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
        build_tile_mesh(*tm.map, mesh);
        mesh.visible = tm.visible;

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
// RTTI
// ---------------------------------------------------------------------------

extern "C" void _rtti_init_tilemap_system()
{
    entt::meta_factory<nb::tilemap_system>{}
        .type("tilemap_system"_hs)
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "tilemap_system",
            .type_class = rtti::TYPE_CLASS_SYSTEM
        })
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::tilemap_system>>{rtti::ctx_systems()}
        .type("tilemap_system_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::tilemap_system>>()
        .conv<std::shared_ptr<nb::system>>();

    ctilemap::_ensure_rtti();
}
