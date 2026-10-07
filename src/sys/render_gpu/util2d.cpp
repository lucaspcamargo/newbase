#include "newbase/render/vertex.hpp"
#include <newbase/sys/render_gpu/pipelines.hpp>
#include <newbase/sys/render_gpu/util2d.hpp>
#include <newbase/sys/render_gpu/shaders.hpp>
#include <newbase/sys/render_gpu/copy_scope.hpp>
#include <newbase/render/batcher2d.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>

#include <SDL3/SDL_gpu.h>
#include <entt/core/hashed_string.hpp>
#include <cstdint>
#include <cstring>


using namespace nb;
using namespace nb::gpu;
using entt::operator""_hs;


bool shaders2d::load(SDL_GPUDevice *dev)
{
    vert = rman().load_sync<rshader>("_nb_core/slang/vertex2d.vert.slang"_hs);
    frag_col = rman().load_sync<rshader>("_nb_core/slang/untextured.frag.slang"_hs);
    frag_tex = rman().load_sync<rshader>("_nb_core/slang/textured.frag.slang"_hs);

    return vert && frag_col && frag_tex;
}


bool shaders2d::prepare(std::function<bool(rshader*)> cb)
{
    return cb(vert.get()) && cb(frag_col.get()) && cb(frag_tex.get());
}


void buffers2d::init(SDL_GPUDevice *dev)
{
    assert(dev);

    SDL_GPUTransferBufferCreateInfo tv_info {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = DEFAULT_CAPACITY_2D_VTX,
        .props = 0
    };
    buf_trans_v = SDL_CreateGPUTransferBuffer(dev, &tv_info);
    cap_trans_v = DEFAULT_CAPACITY_2D_VTX;

    SDL_GPUTransferBufferCreateInfo ti_info {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = DEFAULT_CAPACITY_2D_IND,
        .props = 0
    };
    buf_trans_i = SDL_CreateGPUTransferBuffer(dev, &ti_info);
    cap_trans_i = DEFAULT_CAPACITY_2D_IND;

    // no GPU buffers yet
    buf_gpu_v = buf_gpu_i = nullptr;
}


void buffers2d::teardown(SDL_GPUDevice *dev)
{
    assert(dev);
    if(buf_trans_v)
    {
        SDL_UnmapGPUTransferBuffer(dev, buf_trans_v);
        SDL_ReleaseGPUTransferBuffer(dev, buf_trans_v);
        buf_trans_v = nullptr;
    }
    if(buf_trans_i)
    {
        SDL_UnmapGPUTransferBuffer(dev, buf_trans_i);
        SDL_ReleaseGPUTransferBuffer(dev, buf_trans_i);
        buf_trans_i = nullptr;
    }
    if(buf_gpu_v)
    {
        SDL_ReleaseGPUBuffer(dev, buf_gpu_v);
        buf_gpu_v = nullptr;
    }
    if(buf_gpu_i)
    {
        SDL_ReleaseGPUBuffer(dev, buf_gpu_i);
        buf_gpu_i = nullptr;
    }
}


void buffers2d::upload(SDL_GPUDevice *dev, SDL_GPUCommandBuffer *cmd, const render::batcher2d &batcher, SDL_GPUCopyPass *curr_cpy)
{
    auto bytes_v = batcher.data().verts.size()*sizeof(render::vertex2d);
    auto bytes_i = batcher.data().inds.size()*sizeof(uint16_t);
    if(!bytes_v || !bytes_i)
        return;  // no data!

    // check if current capacity is enough
    // otherwise allocate new buffer and replace it
    if(cap_trans_v < bytes_v)
    {
        auto new_sz = cap_trans_v * 2;
        while(new_sz < bytes_v)
            new_sz *= 2;
        if(buf_trans_v)
            SDL_ReleaseGPUTransferBuffer(dev, buf_trans_v);
        SDL_GPUTransferBufferCreateInfo info {};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size  = new_sz;
        buf_trans_v = SDL_CreateGPUTransferBuffer(dev, &info);
        cap_trans_v = new_sz;
    }
    if(cap_trans_i < bytes_i)
    {
        auto new_sz = cap_trans_i * 2;
        while(new_sz < bytes_i)
            new_sz *= 2;
        if(buf_trans_i)
            SDL_ReleaseGPUTransferBuffer(dev, buf_trans_i);
        SDL_GPUTransferBufferCreateInfo info {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = new_sz,
            .props = 0
        };
        buf_trans_i = SDL_CreateGPUTransferBuffer(dev, &info);
        cap_trans_i = new_sz;
    }

    if (!buf_trans_v || !buf_trans_i) {
        log::error("[render_gpu] buffers2d: failed to allocate GPU transfer buffers!");
        return;
    }

    // copy data to transfer buffers

    uint8_t* map_v = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(dev, buf_trans_v, true));
    if(map_v)
    {
        memcpy(map_v, batcher.data().verts.data(), bytes_v);
    }
    else
    {
        log::error("[render_gpu] buffers2d: failed to map vertex transfer buffer!");
        return;
    }
    SDL_UnmapGPUTransferBuffer(dev, buf_trans_v);

    uint8_t* map_i = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(dev, buf_trans_i, true));
    if(map_i)
    {
        memcpy(map_i, batcher.data().inds.data(), bytes_i);
    }
    else
    {
        log::error("[render_gpu] buffers2d: failed to map index transfer buffer!");
        return;
    }
    SDL_UnmapGPUTransferBuffer(dev, buf_trans_i);

    // ensure gpu buffers exist and fit the data
    if(buf_gpu_v_sz < bytes_v)
    {
        auto new_sz = cap_trans_v;
        if(buf_gpu_v)
        {
            SDL_ReleaseGPUBuffer(dev, buf_gpu_v);
            buf_gpu_v = nullptr;
        }
        SDL_GPUBufferCreateInfo info {
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size  = new_sz,
            .props = 0
        };
        buf_gpu_v = SDL_CreateGPUBuffer(dev, &info);
        buf_gpu_v_sz = new_sz;
    }
    if(buf_gpu_i_sz < bytes_i)
    {
        auto new_sz = cap_trans_i;
        if(buf_gpu_i)
        {
            SDL_ReleaseGPUBuffer(dev, buf_gpu_i);
            buf_gpu_i = nullptr;
        }
        SDL_GPUBufferCreateInfo info {
            .usage = SDL_GPU_BUFFERUSAGE_INDEX,
            .size  = new_sz,
            .props = 0
        };
        buf_gpu_i = SDL_CreateGPUBuffer(dev, &info);
        buf_gpu_i_sz = new_sz;
    }

    // create and submit new copy pass
    copy_scope cpy {cmd, curr_cpy};

    SDL_GPUTransferBufferLocation vert_src = {
        .transfer_buffer = buf_trans_v,
        .offset          = 0
    };
    SDL_GPUBufferRegion vert_dst = {
        .buffer          = buf_gpu_v,
        .offset          = 0,
        .size            = static_cast<uint32_t>(bytes_v)
    };
    SDL_UploadToGPUBuffer(
        cpy,
        &vert_src,
        &vert_dst,
        true
    );

    SDL_GPUTransferBufferLocation ind_src = {
        .transfer_buffer = buf_trans_i,
        .offset          = 0
    };
    SDL_GPUBufferRegion ind_dst = {
        .buffer          = buf_gpu_i,
        .offset          = 0,
        .size            = static_cast<uint32_t>(bytes_i)
    };
    SDL_UploadToGPUBuffer(
        cpy,
        &ind_src,
        &ind_dst,
        true
    );
}


bool nb::gpu::command2d_get_pipeline(const render::command2d &cmd, pipeline_desc &desc, shaders2d &shaders)
{
    desc = pipeline_desc {};

    // shaders (based on whether we have a texture)
    const auto& active_frag = (cmd.texture!=-1) ? shaders.frag_tex : shaders.frag_col;
    desc.vert = (SDL_GPUShader*) shaders.vert->rptr;
    desc.frag = (SDL_GPUShader*) active_frag->rptr;
    if(!desc.vert || !desc.frag)
        return false;

    const auto& vert_refl = shaders.vert->meta;

    desc.setup_vertex_input( render::vertex_type_descriptor::descriptor_table()
                                .at(render::vertex_type::STANDARD_2D), vert_refl);


    // assemble vertex input state
    desc.cull_mode   = SDL_GPU_CULLMODE_NONE;
    desc.blend       = cmd.blend;
    desc.depth_test  = false;
    desc.depth_write = false;
    desc.depth_clip  = false; // TEST

    return true;
}



/*
 OLD, MANUAL 2D VTX LAYOUT:::

 // vertex buffer layout (render::vertex2d interleaved)
 desc.vert_bufs[0].slot = 0;
 desc.vert_bufs[0].pitch = sizeof(render::vertex2d);
 desc.vert_bufs[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
 desc.vert_buf_count  = 1;

 // vertex attribute layout
 SDL_GPUVertexAttribute *attributes = desc.vert_attrs;
 // pos
 attributes[0].location = 0;
 attributes[0].buffer_slot = 0;
 attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
 attributes[0].offset = offsetof(render::vertex2d, pos);
 // col
 attributes[1].location = 1;
 attributes[1].buffer_slot = 0;
 attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
 attributes[1].offset = offsetof(render::vertex2d, color);
 // uv
 attributes[2].location = 2;
 attributes[2].buffer_slot = 0;
 attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
 attributes[2].offset = offsetof(render::vertex2d, uv);
 // count
 desc.vert_attr_count = static_cast<uint16_t>(3);

 */
