#include <newbase/geom/picker2d.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/log.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/camera.hpp>
#include <newbase/components/sprite.hpp>
#include <newbase/components/mesh2d.hpp>
#include <newbase/components/layers.hpp>
#include <newbase/components/particle_emitter.hpp>
#include <newbase/res/sprite.hpp>

using namespace nb;

entt::entity geom::picker_2d::pick(const render_layer &layer, float vp_x, float vp_y)
{
    auto *sc = engine::instance().find_scene(layer.scene_id);
    if (!sc) { log::warn("[pick] no scene"); return entt::null; }

    const auto &vp = layer.viewport;

    // Viewport-local → window → world
    const float win_x = vp_x + vp.x;
    const float win_y = vp_y + vp.y;
    const float vp_cx = vp.x + vp.w * 0.5f;
    const float vp_cy = vp.y + vp.h * 0.5f;

    float cam_cx = 0.f, cam_cy = 0.f, zoom = 1.f;
    auto &reg = sc->registry();
    if (layer.camera != entt::null)
    {
        // TODO get world space coords by camera bounds calc
        if (auto *sp  = reg.try_get<cspatial>(layer.camera)) { cam_cx = sp->pos.x; cam_cy = sp->pos.y; }
        if (auto *cam = reg.try_get<ccamera> (layer.camera)) { zoom = cam->cam2d.scale; }
    }

    const float wx = (win_x - vp_cx) / zoom + cam_cx;
    const float wy = (win_y - vp_cy) / zoom + cam_cy;

    log::info("[pick] vp(%.0f,%.0f) win(%.0f,%.0f) world(%.1f,%.1f) cam(%.1f,%.1f) zoom=%.2f vp_rect=%d,%d %dx%d",
        vp_x, vp_y, win_x, win_y, wx, wy, cam_cx, cam_cy, zoom, vp.x, vp.y, vp.w, vp.h);

    entt::entity best  = entt::null;
    float        best_z = std::numeric_limits<float>::max();

    for (auto [id, spatial] : reg.view<const cspatial>().each())
    {
        // Layer mask check
        const auto *lyr_comp = reg.try_get<clayers>(id);
        const uint32_t entity_mask = lyr_comp ? lyr_comp->mask : clayers::MASK_DEFAULT;
        if (!(entity_mask & layer.layer_mask)) continue;

        if (auto *sprite = reg.try_get<const csprite>(id))
        {
            if (!sprite->visible || !sprite->spr) continue;
            auto &spr = *sprite->spr;

            glm::vec2 dims = spr.dims;
            if (dims == glm::vec2{-1.f, -1.f})
            {
                const glm::vec4 &csr = sprite->current_source_rect;
                if (csr.z > 0.f)
                    dims = { csr.z, csr.w };
                else if (spr.tex && spr.tex->uploaded && spr.tex->rptr)
                    dims = { spr.tex->width, spr.tex->height };
                else continue;
            }

            // Transform pick point into local space and test against sprite quad
            const glm::vec4 local = glm::inverse(spatial.world) * glm::vec4{wx, wy, 0.f, 1.f};
            const float ql = -spr.anchor.x * dims.x;
            const float qt = -spr.anchor.y * dims.y;
            const bool hit = local.x >= ql && local.x <= ql + dims.x &&
                             local.y >= qt && local.y <= qt + dims.y;
            log::info("[pick]   sprite eid=%x pos=(%.1f,%.1f,%.1f) dims=(%.0fx%.0f) local=(%.1f,%.1f) quad=[%.1f..%.1f, %.1f..%.1f] hit=%d",
                entt::to_integral(id), spatial.pos.x, spatial.pos.y, spatial.pos.z,
                dims.x, dims.y, local.x, local.y, ql, ql+dims.x, qt, qt+dims.y, hit);
            if (hit && spatial.pos.z < best_z) { best_z = spatial.pos.z; best = id; }
        }
        else if (auto *mesh = reg.try_get<const cmesh2d>(id))
        {
            if (!mesh->visible || !mesh->geom || mesh->geom->empty()) continue;

            const glm::vec4 local4 = glm::inverse(spatial.world) * glm::vec4{wx, wy, 0.f, 1.f};
            const glm::vec2 lp { local4.x, local4.y };
            const auto &geom  = *mesh->geom;
            const auto &verts = geom.vertices;

            // Sign of cross product for point-in-triangle test
            auto tri_hit = [&](int i0, int i1, int i2) {
                const glm::vec2 a{verts[i0].pos}, b{verts[i1].pos}, c{verts[i2].pos};
                const float d1 = (lp.x-b.x)*(a.y-b.y) - (a.x-b.x)*(lp.y-b.y);
                const float d2 = (lp.x-c.x)*(b.y-c.y) - (b.x-c.x)*(lp.y-c.y);
                const float d3 = (lp.x-a.x)*(c.y-a.y) - (c.x-a.x)*(lp.y-a.y);
                return !((d1<0||d2<0||d3<0) && (d1>0||d2>0||d3>0));
            };

            bool hit = false;
            if (!geom.indices.empty())
            {
                for (size_t i = 0; i+2 < geom.indices.size() && !hit; i += 3)
                    hit = tri_hit(geom.indices[i], geom.indices[i+1], geom.indices[i+2]);
            }
            else
            {
                for (size_t i = 0; i+2 < verts.size() && !hit; i += 3)
                    hit = tri_hit((int)i, (int)i+1, (int)i+2);
            }
            log::info("[pick]   mesh  eid=%x verts=%zu hit=%d", entt::to_integral(id), verts.size(), hit);
            if (hit && spatial.pos.z < best_z) { best_z = spatial.pos.z; best = id; }
        }
        else if (auto *emit = reg.try_get<const cparticle_emitter>(id))
        {
            // Pick if within emission area radius (pos_variance magnitude, minimum 24px)
            float radius = 24.f;
            if (emit->res)
            {
                const glm::vec2 &pv = emit->res->emitter.pos_variance;
                radius = std::max(24.f, glm::length(pv));
            }
            const float dx = wx - spatial.pos.x, dy = wy - spatial.pos.y;
            const bool hit = dx*dx + dy*dy <= radius*radius;
            log::info("[pick]   emit  eid=%x pos=(%.1f,%.1f) radius=%.1f hit=%d",
                entt::to_integral(id), spatial.pos.x, spatial.pos.y, radius, hit);
            if (hit && spatial.pos.z < best_z) { best_z = spatial.pos.z; best = id; }
        }
        else
        {
            // No visual component: small fixed-radius hit area
            constexpr float HIT_RADIUS = 8.f;
            const float dx = wx - spatial.pos.x, dy = wy - spatial.pos.y;
            const bool hit = dx*dx + dy*dy <= HIT_RADIUS*HIT_RADIUS;
            log::info("[pick]   bare  eid=%x pos=(%.1f,%.1f,%.1f) dist=%.1f hit=%d",
                entt::to_integral(id), spatial.pos.x, spatial.pos.y, spatial.pos.z,
                sqrtf(dx*dx+dy*dy), hit);
            if (hit && spatial.pos.z < best_z) { best_z = spatial.pos.z; best = id; }
        }
    }

    log::info("[pick] result: %s (eid=%x)", best == entt::null ? "null" : "hit", entt::to_integral(best));
    return best;
}
