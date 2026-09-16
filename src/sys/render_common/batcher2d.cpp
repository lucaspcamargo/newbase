#include <cstdint>
#include <newbase/sys/render_common/batcher2d.hpp>

using namespace nb;


static int32_t texture_add(render::data2d &data, std::shared_ptr<rtexture> tex);

void render::batcher2d::clear()
{
    m_comms = {};
    m_data = {};
}

void render::batcher2d::add_geom(vertex2d *verts, uint32_t vcount, uint16_t *inds, uint32_t icount,
              std::shared_ptr<rtexture> tex, blendmode2d blend, void *clip)
{
    // nothing to render?
    if (!vcount or !icount)
        return;

    const int32_t tidx = texture_add(m_data, tex);

    bool append = false;
    if(m_comms.size())
    {
        auto &back = m_comms.back();
        if(back.texture == tidx && back.blend == blend && back.clip == clip)
        {
            // (texture, blendmode, clip) match, we could perhaps append to last command
            auto last_vcount =m_data.verts.size() - back.base_vertex;
            if(last_vcount + vcount < UINT16_MAX) // ensure we can index it together
            {
                append = true;
            }
        }
    }

    if(append)
    {

    }
    else
    {
        // just add the data and emit a new draw command
    }

}


int32_t texture_add(render::data2d &data, std::shared_ptr<rtexture> tex)
{
    if(!tex)
        return -1;

    const auto raw = tex.get();

    auto it = data.tex_set.find(raw);
    if( it != data.tex_set.end())
    {
        // texture already in texture set
        return it->second;
    }
    else
    {
        // add texture to set and return index
        // keep reference to texture resource in shared_ptr
        const int32_t new_idx = static_cast<int32_t>(data.tex.size());
        data.tex.emplace_back(std::move(tex));
        data.tex_set.emplace(std::make_pair(raw, new_idx));
        return new_idx;
    }
}
