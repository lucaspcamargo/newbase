#include <cstdint>
#include <newbase/render/batcher2d.hpp>

using namespace nb;


static int32_t texture_add(render::data2d &data, std::shared_ptr<rtexture> tex);

void render::batcher2d::clear()
{
    // clear should retain capacity
    // so we avoid reallocation every frame
    m_comms.clear();
    m_data.clear();
}

void render::batcher2d::add_geom(const vertex2d *verts, uint32_t vcount, const uint16_t *inds, uint32_t icount,
              std::shared_ptr<rtexture> tex, blendmode blend, void *clip)
{
    // nothing to render?
    if (!vcount or !icount)
        return;

    assert(verts && inds); // cannot pass nullptr

    const int32_t tidx = texture_add(m_data, tex);

    bool append = false;
    if(m_comms.size())
    {
        auto &lastcmd = m_comms.back();
        if(lastcmd.texture == tidx && lastcmd.blend == blend && lastcmd.clip == clip)
        {
            // (texture, blendmode, clip) match, we could perhaps append to last command
            auto last_vcount = m_data.verts.size() - lastcmd.base_vertex;
            if(last_vcount + vcount < UINT16_MAX) // ensure we can index it all together
            {
                append = true; // good to go
            }
        }
    }

    if(append)
    {
        // appends new data to last drawing command
        // new vertices go straight in
        // new indices are added with an offset (previous vertex count)
        // last command's index count is increased by the new index count
        // last base vertex and index starts remain the same :)
        auto &lastcmd = m_comms.back();
        auto last_vcount = m_data.verts.size() - lastcmd.base_vertex;
        auto last_idx_count = lastcmd.index_count;
        m_data.verts.insert(m_data.verts.end(), verts, verts+vcount);
        uint16_t offset = static_cast<uint16_t>(last_vcount);
        m_data.inds.reserve(m_data.inds.size() + icount);
        for(uint32_t i = 0; i < icount; i++)
            m_data.inds.push_back(inds[i] + offset);  // not sure this is vectorized, but should be
        lastcmd.index_count += icount;
        lastcmd.vtx_count += vcount;
    }
    else
    {
        // just add the data and emit a new draw command
        uint32_t bvtx = m_data.verts.size();
        uint32_t bidx = m_data.inds.size();
        m_data.verts.insert(m_data.verts.end(), verts, verts+vcount);
        m_data.inds.insert(m_data.inds.end(), inds, inds+icount);

        m_comms.emplace_back(
            command2d{
                bvtx,
                bidx,
                icount,
                vcount,
                tidx,
                blend,
                clip
            }
        );
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
