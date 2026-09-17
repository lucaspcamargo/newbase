#include <cstdint>
#include <newbase/render/collector2d.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/layers.hpp>
#include <newbase/components/sprite.hpp>
#include <newbase/components/mesh2d.hpp>

#include "entt/entt.hpp"
#include "newbase/render/batcher2d.hpp"

using namespace nb;

void render::collector2d::clear()
{
    // no state for now
}

void render::collector2d::collect(batcher2d &target, scene& scene, const render_layer& layer, const glm::mat4 &viewproj)
{
    entt::registry &reg = scene.registry();
    const auto layer_mask = layer.layer_mask;

    reg.sort<cspatial>([](const cspatial &lhs, const cspatial &rhs) {
        return lhs.pos[2] > rhs.pos[2];
    });

    auto spatial_view = reg.view<const cspatial>();
    for (auto [id, spatial] : spatial_view.each())
    {
        const uint32_t entity_mask = [&] {
            auto* lyr = reg.try_get<clayers>(id);
            return lyr ? lyr->mask : clayers::MASK_DEFAULT;
        }();
        if (!(entity_mask & layer_mask))
            continue;

        if (auto* sprite = reg.try_get<const csprite>(id))
        {
            if (!sprite->visible) continue;
            auto spr_res = sprite->spr;
            if (!spr_res) continue;
            auto tex = spr_res->tex;
            if (!tex) continue;

            const glm::vec4& csr = sprite->current_source_rect;
            SDL_FRect src_rect_val;
            const SDL_FRect* src_rect = nullptr;
            bool subrect = (csr.z > 0.f);

            glm::vec2 dims = spr_res->dims;

            // get the sprite dimensions in pixels
            // these are also in world coordinates, before scaling
            if (dims == glm::vec2{-1.0f, -1.0f})
                dims = subrect ? glm::vec2{csr.z, csr.w} : glm::vec2{tex->width, tex->height};

            const glm::vec4 tl {-spr_res->anchor.x * dims.x, -spr_res->anchor.y * dims.y, 0.0f, 1.0f};
            const glm::vec4 tr {-spr_res->anchor.x * dims.x + dims.x, -spr_res->anchor.y * dims.y, 0.0f, 1.0f};
            const glm::vec4 bl {-spr_res->anchor.x * dims.x , -spr_res->anchor.y * dims.y + dims.y, 0.0f, 1.0f};
            const glm::vec4 br {-spr_res->anchor.x * dims.x + dims.x, -spr_res->anchor.y * dims.y + dims.y, 0.0f, 1.0f};

            glm::vec2 tl_uv {0.0, 0.0};
            glm::vec2 tr_uv {1.0, 0.0};
            glm::vec2 bl_uv {0.0, 1.0};
            glm::vec2 br_uv {1.0, 1.0};

            if(subrect)
            {
                tl_uv = {csr.x / tex->width, csr.y / tex->height};
                tr_uv = {(csr.x+csr.z) / tex->width, csr.y / tex->height};
                bl_uv = {csr.x / tex->width, (csr.y+csr.w) / tex->height};
                br_uv = {(csr.x+csr.z) / tex->width, (csr.y+csr.w) / tex->height};
            }

            auto snap  = [sprite](glm::vec2 pos){ return sprite->pixel_snap? glm::round(pos): pos; };
            const vertex2d vertices[4] = {
                {
                    snap(glm::vec2{viewproj * spatial.world * tl}),
                    sprite->color,
                    tl_uv
                },
                {
                    snap(glm::vec2{viewproj * spatial.world * tr}),
                    sprite->color,
                    tr_uv
                },
                {
                    snap(glm::vec2{viewproj * spatial.world * bl}),
                    sprite->color,
                    bl_uv
                },
                {
                    snap(glm::vec2{viewproj * spatial.world * br}),
                    sprite->color,
                    br_uv
                }
            };

            const uint16_t inds[6] = {0, 2, 1, 1, 2, 3};

            target.add_geom(vertices, 4, inds, 6, tex, blendmode2d::BLEND); // TODO blendmodes
        }
        else if (auto* mesh = reg.try_get<const cmesh2d>(id))
        {
            if (!mesh->visible) continue;
            if (!mesh->geom || mesh->geom->empty()) continue;

            const auto& src = mesh->geom->vertices;
            transform_buf.reserve(src.size());
            transform_buf.clear();
            for (size_t i = 0; i < src.size(); ++i)
            {
                const auto& v = src[i];
                const auto pos = glm::vec2{ viewproj * (spatial.world * glm::vec4(v.pos.x, v.pos.y, 0.f, 1.f)) };
                transform_buf.push_back( vertex2d {
                    mesh->pixel_snap? glm::round(pos):pos,
                    v.color,
                    v.uv
                });
            }

            const uint16_t *ind_data = mesh->geom->indices.data();
            uint32_t ind_len = mesh->geom->indices.size();
            if(!ind_len)
            {
                // there are no indices on the mesh data, we need to use our fake ones
                if (ind_buf_seq.size() < transform_buf.size())
                {
                    ind_buf_seq.reserve(transform_buf.size());
                    for( int i = ind_buf_seq.size(); i < transform_buf.size(); i++)
                    {
                        ind_buf_seq.push_back(i);
                    }
                }
                ind_data = ind_buf_seq.data();
                ind_len = transform_buf.size();
            }

            target.add_geom(transform_buf.data(), transform_buf.size(), ind_data, ind_len, mesh->tex,
                            mesh->blend_mode );
        }
    }
}
