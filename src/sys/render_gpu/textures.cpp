#include "SDL3/SDL_gpu.h"
#include <newbase/sys/render_gpu/textures.hpp>
#include <newbase/log.hpp>
#include <cstring>

using namespace nb;
using namespace nb::gpu;


static SDL_GPUTextureFormat  get_srgb_sampling_equivalent(SDL_GPUTextureFormat fmt);

// TODO transfer buffer pool?

void texture_manager::init(SDL_GPUDevice *device, bool dump_formats)
{
    for (int fmt = (SDL_GPU_TEXTUREFORMAT_INVALID+1); fmt <= SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT; ++fmt)
    {
        auto format = static_cast<SDL_GPUTextureFormat>(fmt);

        if (SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
            m_support.samplers.push_back(format);
        }
        if (SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) {
            m_support.color_targets.push_back(format);
        }
        if (SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            m_support.depth_stencil_targets.push_back(format);
        }
    }

    //m_depth_float = m_support.depth_stencil_targets.

    if(dump_formats)
    {
        auto join = [&](const auto &fmt_vec) -> std::string {
            std::string ret;
            for (auto fmt : fmt_vec)
            {
                ret += texture_format_to_str(fmt);
                ret += " ";
            }
            return ret;
        };

        log::info("[render_gpu] textures: supported sampler formats: %s",
                  join(m_support.samplers).c_str());
        log::info("[render_gpu] textures: supported color target formats: %s",
                  join(m_support.color_targets).c_str());
        log::info("[render_gpu] textures: supported depth/stencil target formats: %s",
                  join(m_support.depth_stencil_targets).c_str());
    }
}

bool texture_manager::prepare(SDL_GPUDevice * device, SDL_GPUCommandBuffer *cmd, rtexture *tex, SDL_GPUCopyPass *cpy)
{
    if(!tex)
        return false;

    if(tex->uploaded && tex->rptr)
        return true;

    if(!tex->surf)
        return false;

    auto gfmt = SDL_GetGPUTextureFormatFromPixelFormat(tex->surf->format);
    if(tex->srgb_conversion)
        gfmt = get_srgb_sampling_equivalent(gfmt);
    SDL_Surface *cvt {nullptr};
    auto &gfmt_list = m_support.color_targets;
    if(std::find(gfmt_list.begin(), gfmt_list.end(), gfmt) == gfmt_list.end())
    {
        // cannot use directly as a sampler
        // needs conversion
        // TODO
    }
    auto bytes = tex->surf->pitch*tex->surf->h;
    uint32_t width = tex->surf->w;
    uint32_t height = tex->surf->h;

    SDL_GPUTextureCreateInfo tex_info = {};
    tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    tex_info.format = gfmt;
    tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_info.width = width;
    tex_info.height = height;
    tex_info.layer_count_or_depth = 1;
    tex_info.num_levels = 1;
    tex_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    tex_info.props = 0;

    SDL_GPUTexture* gtex = SDL_CreateGPUTexture(device, &tex_info);
    if(!gtex)
        log::error("[render_gpu] texture_manager: prepare: cannot create texture: %s", SDL_GetError());

    auto filter = tex->nearest? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = filter;
    sampler_info.mag_filter = filter;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;

    SDL_GPUSampler* gsamp = SDL_CreateGPUSampler(device, &sampler_info);
    if(!gsamp)
        log::error("[render_gpu] texture_manager: prepare: cannot create sampler: %s", SDL_GetError());

    SDL_GPUTransferBufferCreateInfo tb_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (uint32_t) bytes
    };
    SDL_GPUTransferBuffer* transfer_buf = SDL_CreateGPUTransferBuffer(device, &tb_info);

    void* map_ptr = SDL_MapGPUTransferBuffer(device, transfer_buf, false);
    std::memcpy(map_ptr, tex->surf->pixels, bytes);
    SDL_UnmapGPUTransferBuffer(device, transfer_buf);

    copy_scope cpy_scope(cmd, cpy);

    SDL_GPUTextureTransferInfo src = { .transfer_buffer = transfer_buf, .offset = 0 };
    SDL_GPUTextureRegion dst       =
    { .texture = gtex, .w = width, .h = height, .d = 1 };
    SDL_UploadToGPUTexture(cpy_scope, &src, &dst, false);

    SDL_ReleaseGPUTransferBuffer(device, transfer_buf);

    // queue existing GPU data for deletion, if any
    if(tex->rptr)
    {
        _texture_destroy(*tex, this);
    }

    tex->rptr = new texture_block { gtex, gsamp, gfmt, width, height};
    tex->width = width;
    tex->height = height;
    tex->uploaded = true;
    tex->on_delete = _texture_destroy;
    tex->on_delete_uptr = this;

    //log::critical("[render_gpu] texture_manager: TEXTURE CREATED ON %p->%p", tex, tex->rptr);

    return true;
}

void texture_manager::cleanup(SDL_GPUDevice * dev)
{
    if(!m_delete.size())
        return;

    typeof(m_delete) local_delete {};

    {
        std::scoped_lock lock(m_delete_mtx);
        std::swap(local_delete, m_delete);
    }

    for(auto &block : local_delete)
    {
        //log::critical("[render_gpu] texture_manager: TEXTURE REAL DESTROY %p", block.gtex);
        if(block.gtex)
            SDL_ReleaseGPUTexture(dev, block.gtex);
        if(block.gsamp)
            SDL_ReleaseGPUSampler(dev, block.gsamp);
    }
}

void texture_manager::teardown(SDL_GPUDevice * dev)
{
    cleanup(dev);
}


void texture_manager::set_non_owning(rtexture *rtex, SDL_GPUTexture *gtex, SDL_GPUTextureFormat gfmt, int w, int h)
{
    if(!rtex || !gtex)
    {
        log::error("[render_gpu] texture_manager: set_non_owning: missing resources!");
        return;
    }

    // create new texture block
    texture_block *new_blk = new texture_block{};
    new_blk->gtex = gtex;
    new_blk->gfmt = gfmt;
    new_blk->gsamp = nullptr;
    new_blk->owns_texture = false;
    new_blk->owns_sampler = false;
    new_blk->w = w;
    new_blk->h = h;

    // destroy whatever texture block might be there
    _texture_destroy(*rtex, this);

    // assign new block and overwrite deletion info if any
    rtex->rptr = new_blk;
    rtex->width = w;
    rtex->height = h;
    rtex->uploaded = true;
    rtex->on_delete = _texture_destroy;
    rtex->on_delete_uptr = this;
}


bool texture_manager::color_target_setup(SDL_GPUDevice *gdev, rtexture *rtex, int w, int h)
{
    if(!rtex || !w || !h)
    {
        log::error("[render_gpu] texture_manager: color_target_setup: no texture or invalid dimensions!");
        return false;
    }

    texture_block* curr = static_cast<texture_block*>(rtex->rptr);

    if(curr)
    {
        // already fine
        if(curr->w == w && curr->h == h)
            return true;

        // needs to be rebuilt
        _texture_destroy(*rtex, this);
    }

    auto gfmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUTextureCreateInfo info {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = gfmt,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = (uint32_t) w,
        .height = (uint32_t) h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    SDL_GPUTexture *gtex = SDL_CreateGPUTexture(gdev, &info);
    if(!gtex)
        log::error("[render_gpu] texture_manager: color_target_setup: cannot create texture: %s", SDL_GetError());

    auto filter = rtex->nearest? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = filter;
    sampler_info.mag_filter = filter;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    SDL_GPUSampler* gsamp = SDL_CreateGPUSampler(gdev, &sampler_info);
    if(!gsamp)
        log::error("[render_gpu] texture_manager: color_target_setup: cannot create sampler: %s", SDL_GetError());

    // create new texture block
    texture_block *new_blk = new texture_block{};
    new_blk->gtex = gtex;
    new_blk->gfmt = gfmt;
    new_blk->gsamp = gsamp;
    new_blk->owns_texture = true;
    new_blk->owns_sampler = true;
    new_blk->w = w;
    new_blk->h = h;

    rtex->rptr = new_blk;
    rtex->width = w;
    rtex->height = h;
    rtex->uploaded = true;
    rtex->on_delete = _texture_destroy;
    rtex->on_delete_uptr = this;

    return true;
}


bool texture_manager::depth_target_setup(SDL_GPUDevice *gdev, rtexture *rtex, int w, int h, bool needs_sampling)
{
    if(!rtex || !w || !h)
    {
        log::error("[render_gpu] texture_manager: depth_target_setup: no texture or invalid dimensions!");
        return false;
    }

    texture_block* curr = static_cast<texture_block*>(rtex->rptr);

    if(curr)
    {
        // already fine
        if(curr->w == w && curr->h == h)
            return true;

        // needs to be rebuilt
        _texture_destroy(*rtex, this);
    }

    // TODO 16-bit depth may be too basic but it is a start
    //      using this now because it is guaranteed to always be sampleable
    auto gfmt = SDL_GPU_TEXTUREFORMAT_D16_UNORM;

    SDL_GPUTextureCreateInfo info {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = gfmt,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
            (needs_sampling? SDL_GPU_TEXTUREUSAGE_SAMPLER : 0),
        .width = (uint32_t) w,
        .height = (uint32_t) h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    SDL_GPUTexture *gtex = SDL_CreateGPUTexture(gdev, &info);
    if(!gtex)
        log::error("[render_gpu] texture_manager: depth_target_setup: cannot create texture: %s", SDL_GetError());

    auto filter = rtex->nearest? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = filter;
    sampler_info.mag_filter = filter;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    SDL_GPUSampler* gsamp = SDL_CreateGPUSampler(gdev, &sampler_info);
    if(!gsamp)
        log::error("[render_gpu] texture_manager: depth_target_setup: cannot create sampler: %s", SDL_GetError());

    // create new texture block
    texture_block *new_blk = new texture_block{};
    new_blk->gtex = gtex;
    new_blk->gfmt = gfmt;
    new_blk->gsamp = gsamp;
    new_blk->owns_texture = true;
    new_blk->owns_sampler = true;
    new_blk->w = w;
    new_blk->h = h;

    rtex->rptr = new_blk;
    rtex->width = w;
    rtex->height = h;
    rtex->uploaded = true;
    rtex->on_delete = _texture_destroy;
    rtex->on_delete_uptr = this;

    return true;

}


void texture_manager::_texture_destroy(rtexture& tex, void *uptr)
{
    //log::critical("[render_gpu] texture_manager: TEXTURE MARK DESTROY ON %p->%p", &tex, tex.rptr);

    if(!tex.rptr)
        return;

    if(!uptr)
    {
        log::error("[render_gpu] texture_manager: _texture_destroy: no user data!");
        return;
    }


    texture_block *curr = static_cast<texture_block*>(tex.rptr);
    texture_manager *mgr = static_cast<texture_manager*>(uptr);

    if(curr->owns_texture || curr->owns_sampler)
    {
        // store block for deletion
        std::scoped_lock lock(mgr->m_delete_mtx);
        mgr->m_delete.push_back(*curr);
    }
    delete curr;
    tex.rptr = nullptr;
    tex.on_delete = nullptr;
    tex.on_delete_uptr = nullptr;
}



/// helper for getting an SRGB equivalent format
/// returns the same format when an override is not found


SDL_GPUTextureFormat  get_srgb_sampling_equivalent(SDL_GPUTextureFormat fmt)
{
    switch (fmt)
    {
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
            return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
            return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM:
            return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM:
            return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM:
            return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM:
            return SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x4_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_5x4_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x5_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_5x5_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x5_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_6x5_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x5_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_8x5_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x6_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_8x6_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x8_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_8x8_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x5_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_10x5_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x6_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_10x6_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x8_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_10x8_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x10_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_10x10_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x10_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_12x10_UNORM_SRGB;
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x12_UNORM:
            return SDL_GPU_TEXTUREFORMAT_ASTC_12x12_UNORM_SRGB;
        default:
            return fmt;
    }
    return fmt;
}


/// string helper for printing

const char * texture_manager::texture_format_to_str(SDL_GPUTextureFormat fmt)
{
    switch (fmt)
    {
        case SDL_GPU_TEXTUREFORMAT_INVALID:
            return "INVALID";
        case SDL_GPU_TEXTUREFORMAT_A8_UNORM:
            return "A8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R8_UNORM:
            return "R8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R8G8_UNORM:
            return "R8G8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
            return "R8G8B8A8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R16_UNORM:
            return "R16_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R16G16_UNORM:
            return "R16G16_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM:
            return "R16G16B16A16_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM:
            return "R10G10B10A2_UNORM";
        case SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM:
            return "B5G6R5_UNORM";
        case SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM:
            return "B5G5R5A1_UNORM";
        case SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM:
            return "B4G4R4A4_UNORM";
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
            return "B8G8R8A8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM:
            return "BC1_RGBA_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM:
            return "BC2_RGBA_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM:
            return "BC3_RGBA_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM:
            return "BC4_R_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM:
            return "BC5_RG_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM:
            return "BC7_RGBA_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC6H_RGB_FLOAT:
            return "BC6H_RGB_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_BC6H_RGB_UFLOAT:
            return "BC6H_RGB_UFLOAT";
        case SDL_GPU_TEXTUREFORMAT_R8_SNORM:
            return "R8_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R8G8_SNORM:
            return "R8G8_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM:
            return "R8G8B8A8_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R16_SNORM:
            return "R16_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R16G16_SNORM:
            return "R16G16_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SNORM:
            return "R16G16B16A16_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R16_FLOAT:
            return "R16_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT:
            return "R16G16_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
            return "R16G16B16A16_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:
            return "R32_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT:
            return "R32G32_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
            return "R32G32B32A32_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT:
            return "R11G11B10_UFLOAT";
        case SDL_GPU_TEXTUREFORMAT_R8_UINT:
            return "R8_UINT";
        case SDL_GPU_TEXTUREFORMAT_R8G8_UINT:
            return "R8G8_UINT";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT:
            return "R8G8B8A8_UINT";
        case SDL_GPU_TEXTUREFORMAT_R16_UINT:
            return "R16_UINT";
        case SDL_GPU_TEXTUREFORMAT_R16G16_UINT:
            return "R16G16_UINT";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT:
            return "R16G16B16A16_UINT";
        case SDL_GPU_TEXTUREFORMAT_R32_UINT:
            return "R32_UINT";
        case SDL_GPU_TEXTUREFORMAT_R32G32_UINT:
            return "R32G32_UINT";
        case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT:
            return "R32G32B32A32_UINT";
        case SDL_GPU_TEXTUREFORMAT_R8_INT:
            return "R8_INT";
        case SDL_GPU_TEXTUREFORMAT_R8G8_INT:
            return "R8G8_INT";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT:
            return "R8G8B8A8_INT";
        case SDL_GPU_TEXTUREFORMAT_R16_INT:
            return "R16_INT";
        case SDL_GPU_TEXTUREFORMAT_R16G16_INT:
            return "R16G16_INT";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT:
            return "R16G16B16A16_INT";
        case SDL_GPU_TEXTUREFORMAT_R32_INT:
            return "R32_INT";
        case SDL_GPU_TEXTUREFORMAT_R32G32_INT:
            return "R32G32_INT";
        case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_INT:
            return "R32G32B32A32_INT";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
            return "R8G8B8A8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
            return "B8G8R8A8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB:
            return "BC1_RGBA_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM_SRGB:
            return "BC2_RGBA_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB:
            return "BC3_RGBA_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB:
            return "BC7_RGBA_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
            return "D16_UNORM";
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
            return "D24_UNORM";
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
            return "D32_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
            return "D24_UNORM_S8_UINT";
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:
            return "D32_FLOAT_S8_UINT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM:
            return "ASTC_4x4_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x4_UNORM:
            return "ASTC_5x4_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x5_UNORM:
            return "ASTC_5x5_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x5_UNORM:
            return "ASTC_6x5_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM:
            return "ASTC_6x6_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x5_UNORM:
            return "ASTC_8x5_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x6_UNORM:
            return "ASTC_8x6_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x8_UNORM:
            return "ASTC_8x8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x5_UNORM:
            return "ASTC_10x5_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x6_UNORM:
            return "ASTC_10x6_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x8_UNORM:
            return "ASTC_10x8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x10_UNORM:
            return "ASTC_10x10_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x10_UNORM:
            return "ASTC_12x10_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x12_UNORM:
            return "ASTC_12x12_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM_SRGB:
            return "ASTC_4x4_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x4_UNORM_SRGB:
            return "ASTC_5x4_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x5_UNORM_SRGB:
            return "ASTC_5x5_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x5_UNORM_SRGB:
            return "ASTC_6x5_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM_SRGB:
            return "ASTC_6x6_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x5_UNORM_SRGB:
            return "ASTC_8x5_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x6_UNORM_SRGB:
            return "ASTC_8x6_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x8_UNORM_SRGB:
            return "ASTC_8x8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x5_UNORM_SRGB:
            return "ASTC_10x5_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x6_UNORM_SRGB:
            return "ASTC_10x6_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x8_UNORM_SRGB:
            return "ASTC_10x8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x10_UNORM_SRGB:
            return "ASTC_10x10_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x10_UNORM_SRGB:
            return "ASTC_12x10_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x12_UNORM_SRGB:
            return "ASTC_12x12_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_FLOAT:
            return "ASTC_4x4_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x4_FLOAT:
            return "ASTC_5x4_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x5_FLOAT:
            return "ASTC_5x5_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x5_FLOAT:
            return "ASTC_6x5_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x6_FLOAT:
            return "ASTC_6x6_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x5_FLOAT:
            return "ASTC_8x5_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x6_FLOAT:
            return "ASTC_8x6_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x8_FLOAT:
            return "ASTC_8x8_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x5_FLOAT:
            return "ASTC_10x5_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x6_FLOAT:
            return "ASTC_10x6_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x8_FLOAT:
            return "ASTC_10x8_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x10_FLOAT:
            return "ASTC_10x10_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x10_FLOAT:
            return "ASTC_12x10_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x12_FLOAT:
            return "ASTC_12x12_FLOAT";
    }

    return "UNKNOWN";
}
