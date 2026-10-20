#include "SDL3/SDL_gpu.h"
#include "newbase/render/texture.hpp"
#include <newbase/sys/render_gpu/textures.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_pixels.h>
#include <algorithm>
#include <cstring>
#include <limits>

using namespace nb;
using namespace nb::gpu;


static SDL_GPUTextureFormat  get_srgb_sampling_equivalent(SDL_GPUTextureFormat fmt);

static uint32_t mip_dimension(uint32_t value, uint32_t level)
{
    return std::max(1u, value >> level);
}

static bool multiply_size(uint64_t &value, uint64_t factor)
{
    if(factor && value > std::numeric_limits<uint64_t>::max() / factor)
        return false;
    value *= factor;
    return true;
}

static uint32_t bytes_per_pixel(render::texture_format format)
{
    switch(format)
    {
        case render::texture_format::COLOR_RGBA_8888:
        case render::texture_format::COLOR_BGRA_8888:
        case render::texture_format::COLOR_RGBA_8888_SRGB:
        case render::texture_format::COLOR_BGRA_8888_SRGB:
        case render::texture_format::COLOR_RG_16_FLOAT:
            return 4;
        case render::texture_format::COLOR_R_8_UNORM:
            return 1;
        case render::texture_format::COLOR_R_16_FLOAT:
            return 2;
        case render::texture_format::COLOR_RGBA_16_FLOAT:
            return 8;
        default:
            return 0;
    }
}

// TODO transfer buffer pool?
// TODO conversions from engine texture formats to gpu texture formats

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

    // TODO evaluate and store mappings for "dynamic" formats based on support
}

bool texture_manager::prepare(SDL_GPUDevice * device, SDL_GPUCommandBuffer *cmd, rtexture *tex, SDL_GPUCopyPass *cpy)
{
    if(!tex)
        return false;

    const bool target_only = render::has_flag(tex->flags, render::texture_flags::TARGET) &&
        !tex->surf && tex->data.empty();
    if(target_only)
    {
        if(tex->width == 0 || tex->height == 0 || tex->depth == 0 || tex->layer_count == 0)
        {
            log::error("[render_gpu] texture_manager: prepare: target has invalid dimensions");
            return false;
        }

        SDL_GPUTextureType gpu_type = SDL_GPU_TEXTURETYPE_2D;
        uint32_t layer_count_or_depth = tex->layer_count;
        switch(tex->type)
        {
            case render::texture_type::IMAGE_2D:
                if(tex->depth != 1)
                    return false;
                gpu_type = tex->layer_count == 1
                    ? SDL_GPU_TEXTURETYPE_2D : SDL_GPU_TEXTURETYPE_2D_ARRAY;
                    break;
            case render::texture_type::IMAGE_2D_ARRAY:
                gpu_type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
                layer_count_or_depth = tex->layer_count;
            break;
            case render::texture_type::IMAGE_3D:
                if(tex->layer_count != 1)
                    return false;
                gpu_type = SDL_GPU_TEXTURETYPE_3D;
                layer_count_or_depth = tex->depth;
                break;
            case render::texture_type::CUBEMAP:
                if(tex->depth != 1 || tex->layer_count < 6 || tex->layer_count % 6 != 0)
                    return false;
                gpu_type = tex->layer_count == 6
                    ? SDL_GPU_TEXTURETYPE_CUBE : SDL_GPU_TEXTURETYPE_CUBE_ARRAY;
                break;
            default:
                return false;
        }

        const SDL_GPUTextureFormat gfmt = get_gpu_format(tex->format);
        if(gfmt == SDL_GPU_TEXTUREFORMAT_INVALID)
            return false;
        const bool depth_format =
            gfmt == SDL_GPU_TEXTUREFORMAT_D16_UNORM ||
            gfmt == SDL_GPU_TEXTUREFORMAT_D24_UNORM ||
            gfmt == SDL_GPU_TEXTUREFORMAT_D32_FLOAT ||
            gfmt == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ||
            gfmt == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
        const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER |
            (depth_format ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                          : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
        if(!SDL_GPUTextureSupportsFormat(device, gfmt, gpu_type, usage))
        {
            log::error("[render_gpu] texture_manager: prepare: unsupported target texture format/type/usage");
            return false;
        }

        uint32_t num_levels = std::max(1u, static_cast<uint32_t>(tex->mipmaps.size()));
        uint32_t largest_dimension = std::max(tex->width, tex->height);
        if(gpu_type == SDL_GPU_TEXTURETYPE_3D)
            largest_dimension = std::max(largest_dimension, tex->depth);
        uint32_t max_levels = 1;
        while(largest_dimension > 1)
        {
            largest_dimension >>= 1;
            ++max_levels;
        }
        if(render::has_flag(tex->flags, render::texture_flags::GENERATE_MIPMAPS_DEFERRED))
            num_levels = max_levels;
        if(num_levels > max_levels)
            return false;

        auto *curr = static_cast<texture_block*>(tex->rptr);
        if(curr && curr->gtex && curr->gfmt == gfmt && curr->type == gpu_type &&
           curr->w == tex->width && curr->h == tex->height &&
           curr->layer_count_or_depth == layer_count_or_depth &&
           curr->num_levels == num_levels && curr->owns_texture)
        {
            tex->flags &= ~render::texture_flags::NEEDS_UPLOAD;
            tex->flags |= render::texture_flags::UPLOADED;
            return true;
        }

        if(curr)
            _texture_destroy(*tex, this);

        SDL_GPUTextureCreateInfo info = {};
        info.type = gpu_type;
        info.format = gfmt;
        info.usage = usage;
        info.width = tex->width;
        info.height = tex->height;
        info.layer_count_or_depth = layer_count_or_depth;
        info.num_levels = num_levels;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture *gtex = SDL_CreateGPUTexture(device, &info);
        if(!gtex)
        {
            log::error("[render_gpu] texture_manager: prepare: cannot create target texture: %s", SDL_GetError());
            return false;
        }

        auto *block = new texture_block{};
        block->gtex = gtex;
        block->gfmt = gfmt;
        block->type = gpu_type;
        block->w = tex->width;
        block->h = tex->height;
        block->layer_count_or_depth = layer_count_or_depth;
        block->num_levels = num_levels;
        tex->rptr = block;
        tex->flags &= ~render::texture_flags::NEEDS_UPLOAD;
        tex->flags |= render::texture_flags::UPLOADED;
        tex->on_delete = _texture_destroy;
        tex->on_delete_uptr = this;

        return true;
    }

    if(render::has_flag(tex->flags, render::texture_flags::UPLOADED) && tex->rptr)
        return true;

    const bool surface_upload = tex->surf != nullptr;
    if(!surface_upload && tex->data.empty())
        return false;

    if(surface_upload && (!tex->surf->pixels || tex->surf->w <= 0 || tex->surf->h <= 0 || tex->surf->pitch <= 0))
        return false;
    if(!surface_upload && (tex->width == 0 || tex->height == 0 || tex->mipmaps.empty()))
        return false;

    auto gfmt = surface_upload
        ? SDL_GetGPUTextureFormatFromPixelFormat(tex->surf->format)
        : get_gpu_format(tex->format);
    if(surface_upload && tex->srgb_conversion)
        gfmt = get_srgb_sampling_equivalent(gfmt);

    uint32_t width = surface_upload ? static_cast<uint32_t>(tex->surf->w) : tex->width;
    uint32_t height = surface_upload ? static_cast<uint32_t>(tex->surf->h) : tex->height;
    uint32_t depth = surface_upload ? 1 : tex->depth;
    uint32_t layers = surface_upload ? 1 : tex->layer_count;
    SDL_GPUTextureType gpu_type = SDL_GPU_TEXTURETYPE_2D;
    if(!surface_upload)
    {
        switch(tex->type)
        {
            case render::texture_type::IMAGE_2D:
                if(depth != 1 || layers == 0)
                    return false;
                gpu_type = layers == 1 ? SDL_GPU_TEXTURETYPE_2D : SDL_GPU_TEXTURETYPE_2D_ARRAY;
                break;
            case render::texture_type::IMAGE_2D_ARRAY:
                if(depth != 1 || layers == 0)
                    return false;
                gpu_type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
                break;
            case render::texture_type::IMAGE_3D:
                if(depth == 0 || layers != 1)
                    return false;
                gpu_type = SDL_GPU_TEXTURETYPE_3D;
                break;
            case render::texture_type::CUBEMAP:
                if(depth != 1 || layers < 6 || layers % 6 != 0)
                    return false;
                gpu_type = layers == 6 ? SDL_GPU_TEXTURETYPE_CUBE : SDL_GPU_TEXTURETYPE_CUBE_ARRAY;
                break;
            default:
                return false;
        }

    }

    const uint32_t stored_levels = surface_upload ? 1 : static_cast<uint32_t>(tex->mipmaps.size());
    uint32_t largest_dimension = std::max(width, height);
    if(gpu_type == SDL_GPU_TEXTURETYPE_3D)
        largest_dimension = std::max(largest_dimension, depth);
    uint32_t max_levels = 1;
    while(largest_dimension > 1)
    {
        ++max_levels;
        largest_dimension >>= 1;
    }
    if(stored_levels > max_levels)
        return false;

    const bool deferred_mipmaps = render::has_flag(tex->flags, render::texture_flags::TARGET) &&
        render::has_flag(tex->flags, render::texture_flags::GENERATE_MIPMAPS_DEFERRED);
    const bool allocate_mipmaps = deferred_mipmaps ||
        (stored_levels == 1 &&
         render::has_flag(tex->flags, render::texture_flags::GENERATE_MIPMAPS));
    uint32_t allocated_levels = stored_levels;
    if(allocate_mipmaps)
    {
        allocated_levels = max_levels;
    }

    struct mip_upload_layout
    {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
        uint32_t pixels_per_row;
        uint64_t layer_stride;
    };
    std::vector<mip_upload_layout> mip_layouts;
    if(!surface_upload)
    {
        const uint32_t pixel_size = bytes_per_pixel(tex->format);
        if(pixel_size == 0)
            return false;
        mip_layouts.reserve(stored_levels);
        for(uint32_t level = 0; level < stored_levels; ++level)
        {
            const auto &mip = tex->mipmaps[level];
            const uint32_t mip_width = mip_dimension(width, level);
            const uint32_t mip_height = mip_dimension(height, level);
            const uint32_t mip_depth = gpu_type == SDL_GPU_TEXTURETYPE_3D ? mip_dimension(depth, level) : 1;
            if(mip_width > std::numeric_limits<uint32_t>::max() / pixel_size)
                return false;
            const uint32_t row_bytes = mip_width * pixel_size;
            if(row_bytes > std::numeric_limits<uint32_t>::max() - 3u)
                return false;
            const uint32_t padded_row_bytes = (row_bytes + 3u) & ~3u;
            uint64_t tight_layer_size = static_cast<uint64_t>(row_bytes) * mip_height;
            if(!multiply_size(tight_layer_size, mip_depth))
                return false;
            uint64_t padded_layer_size = static_cast<uint64_t>(padded_row_bytes) * mip_height;
            if(!multiply_size(padded_layer_size, mip_depth))
                return false;
            const uint64_t layer_count = gpu_type == SDL_GPU_TEXTURETYPE_3D ? 1 : layers;
            uint64_t tight_total_size = tight_layer_size;
            if(!multiply_size(tight_total_size, layer_count))
                return false;
            const uint64_t padded_stride = gpu_type == SDL_GPU_TEXTURETYPE_CUBE ||
                gpu_type == SDL_GPU_TEXTURETYPE_CUBE_ARRAY
                ? (padded_layer_size + 3u) & ~uint64_t(3u)
                : padded_layer_size;
            uint64_t padded_total_size = padded_stride;
            if(!multiply_size(padded_total_size, layer_count))
                return false;
            const bool tight_layout = mip.size == tight_total_size;
            const bool padded_layout = mip.size == padded_total_size;
            if(mip.offset > tex->data.size() ||
               mip.size > tex->data.size() - mip.offset ||
               (!tight_layout && !padded_layout))
                return false;
            mip_layouts.push_back({
                mip_width,
                mip_height,
                mip_depth,
                tight_layout ? mip_width : padded_row_bytes / pixel_size,
                tight_layout ? tight_layer_size : padded_stride
            });
        }
    }

    SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if(render::has_flag(tex->flags, render::texture_flags::TARGET) || allocate_mipmaps)
        usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if(gfmt == SDL_GPU_TEXTUREFORMAT_INVALID ||
       !SDL_GPUTextureSupportsFormat(device, gfmt, gpu_type, usage))
    {
        log::error("[render_gpu] texture_manager: prepare: unsupported texture format/type/usage");
        return false;
    }

    SDL_GPUTextureCreateInfo tex_info = {};
    tex_info.type = gpu_type;
    tex_info.format = gfmt;
    tex_info.usage = usage;
    tex_info.width = width;
    tex_info.height = height;
    tex_info.layer_count_or_depth = gpu_type == SDL_GPU_TEXTURETYPE_3D ? depth : layers;
    tex_info.num_levels = allocated_levels;
    tex_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    tex_info.props = 0;

    SDL_GPUTexture* gtex = SDL_CreateGPUTexture(device, &tex_info);
    if(!gtex)
    {
        log::error("[render_gpu] texture_manager: prepare: cannot create texture: %s", SDL_GetError());
        return false;
    }

    uint64_t bytes = 0;
    if(surface_upload)
    {
        if(SDL_BYTESPERPIXEL(tex->surf->format) == 0 ||
           tex->surf->pitch % SDL_BYTESPERPIXEL(tex->surf->format) != 0)
        {
            SDL_ReleaseGPUTexture(device, gtex);
            return false;
        }
        bytes = static_cast<uint64_t>(tex->surf->pitch) * height;
    }
    else
    {
        bytes = tex->data.size();
    }
    if(bytes == 0 || bytes > std::numeric_limits<uint32_t>::max())
    {
        log::error("[render_gpu] texture_manager: prepare: invalid transfer buffer size");
        SDL_ReleaseGPUTexture(device, gtex);
        return false;
    }
    SDL_GPUTransferBufferCreateInfo tb_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (uint32_t) bytes
    };
    SDL_GPUTransferBuffer* transfer_buf = SDL_CreateGPUTransferBuffer(device, &tb_info);
    if(!transfer_buf)
    {
        log::error("[render_gpu] texture_manager: prepare: cannot create transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(device, gtex);
        return false;
    }

    void* map_ptr = SDL_MapGPUTransferBuffer(device, transfer_buf, false);
    if(!map_ptr)
    {
        log::error("[render_gpu] texture_manager: prepare: cannot map transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer_buf);
        SDL_ReleaseGPUTexture(device, gtex);
        return false;
    }
    std::memcpy(map_ptr, surface_upload ? tex->surf->pixels : tex->data.data(), static_cast<size_t>(bytes));
    SDL_UnmapGPUTransferBuffer(device, transfer_buf);

    bool upload_valid = true;
    {
        copy_scope cpy_scope(cmd, cpy);
        if(!cpy_scope)
        {
            upload_valid = false;
        }
        else if(surface_upload)
        {
            SDL_GPUTextureTransferInfo src = {
                .transfer_buffer = transfer_buf,
                .offset = 0,
                .pixels_per_row = static_cast<uint32_t>(tex->surf->pitch / SDL_BYTESPERPIXEL(tex->surf->format)),
                .rows_per_layer = height
            };
            SDL_GPUTextureRegion dst = {};
            dst.texture = gtex;
            dst.w = width;
            dst.h = height;
            dst.d = 1;
            SDL_UploadToGPUTexture(cpy_scope, &src, &dst, false);
        }
        else
        {
            for(uint32_t level = 0; upload_valid && level < stored_levels; ++level)
            {
                const auto &mip = tex->mipmaps[level];
                const auto &layout = mip_layouts[level];
                const uint64_t layer_count = gpu_type == SDL_GPU_TEXTURETYPE_3D ? 1 : layers;
                for(uint32_t layer = 0; layer < layer_count; ++layer)
                {
                    SDL_GPUTextureTransferInfo src = {};
                    src.transfer_buffer = transfer_buf;
                    src.offset = static_cast<uint32_t>(mip.offset + layer * layout.layer_stride);
                    src.pixels_per_row = layout.pixels_per_row;
                    src.rows_per_layer = layout.height;
                    SDL_GPUTextureRegion dst = {};
                    dst.texture = gtex;
                    dst.mip_level = level;
                    dst.layer = layer;
                    dst.w = layout.width;
                    dst.h = layout.height;
                    dst.d = layout.depth;
                    SDL_UploadToGPUTexture(cpy_scope, &src, &dst, false);
                }
            }
        }
    }

    SDL_ReleaseGPUTransferBuffer(device, transfer_buf);
    if(!upload_valid)
    {
        log::error("[render_gpu] texture_manager: prepare: invalid texture payload layout");
        SDL_ReleaseGPUTexture(device, gtex);
        return false;
    }

    // queue existing GPU data for deletion, if any
    if(tex->rptr)
    {
        _texture_destroy(*tex, this);
    }

    auto *block = new texture_block{};
    block->gtex = gtex;
    block->gfmt = gfmt;
    block->type = gpu_type;
    block->w = width;
    block->h = height;
    block->layer_count_or_depth = gpu_type == SDL_GPU_TEXTURETYPE_3D ? depth : layers;
    block->num_levels = allocated_levels;
    tex->rptr = block;
    tex->width = width;
    tex->height = height;
    tex->flags &= ~render::texture_flags::NEEDS_UPLOAD;
    tex->flags |= render::texture_flags::UPLOADED;
    tex->on_delete = _texture_destroy;
    tex->on_delete_uptr = this;
    if(allocate_mipmaps &&
       render::has_flag(tex->flags, render::texture_flags::GENERATE_MIPMAPS) &&
       std::find(m_mipmap_generation.begin(), m_mipmap_generation.end(), tex) == m_mipmap_generation.end())
        m_mipmap_generation.push_back(tex);

    //log::critical("[render_gpu] texture_manager: TEXTURE CREATED ON %p->%p", tex, tex->rptr);

    return true;
}

void texture_manager::generate_mipmaps(SDL_GPUCommandBuffer *cmd)
{
    for(auto *tex : m_mipmap_generation)
    {
        if(!tex || !tex->rptr || !render::has_flag(tex->flags, render::texture_flags::GENERATE_MIPMAPS))
            continue;
        auto *block = static_cast<texture_block*>(tex->rptr);
        SDL_GenerateMipmapsForGPUTexture(cmd, block->gtex);
    }
    m_mipmap_generation.clear();
}

void texture_manager::generate_mipmaps_deferred(SDL_GPUCommandBuffer *cmd, rtexture *tex)
{
    if(!cmd || !tex ||
       !render::has_flag(tex->flags, render::texture_flags::TARGET) ||
       !render::has_flag(tex->flags, render::texture_flags::GENERATE_MIPMAPS_DEFERRED) ||
       !tex->rptr)
        return;

    generate_mipmaps_for_target(cmd, tex);
}

bool texture_manager::generate_mipmaps_for_target(SDL_GPUCommandBuffer *cmd, rtexture *tex)
{
    if(!cmd || !tex ||
       !render::has_flag(tex->flags, render::texture_flags::TARGET) ||
       !tex->rptr)
    {
        log::error("[render_gpu] texture_manager: cannot generate target mipmaps for invalid texture");
        return false;
    }

    auto *block = static_cast<texture_block*>(tex->rptr);
    if(!block->gtex || block->num_levels < 2)
        return false;

    SDL_GenerateMipmapsForGPUTexture(cmd, block->gtex);
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
    new_blk->owns_texture = false;
    new_blk->w = w;
    new_blk->h = h;

    // destroy whatever texture block might be there
    _texture_destroy(*rtex, this);

    // assign new block and overwrite deletion info if any
    rtex->rptr = new_blk;
    rtex->width = w;
    rtex->height = h;
    rtex->flags &= ~render::texture_flags::NEEDS_UPLOAD;
    rtex->flags |= render::texture_flags::UPLOADED;
    rtex->on_delete = _texture_destroy;
    rtex->on_delete_uptr = this;
}


bool texture_manager::color_target_setup(SDL_GPUDevice *gdev, rtexture *rtex,
                                         render::texture_format native_fmt,
                                         int w, int h)
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

    auto gfmt = get_gpu_format(native_fmt);

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

    // create new texture block
    texture_block *new_blk = new texture_block{};
    new_blk->gtex = gtex;
    new_blk->gfmt = gfmt;
    new_blk->owns_texture = true;
    new_blk->w = w;
    new_blk->h = h;
    new_blk->type = SDL_GPU_TEXTURETYPE_2D;
    new_blk->layer_count_or_depth = 1;
    new_blk->num_levels = 1;

    rtex->rptr = new_blk;
    rtex->width = w;
    rtex->height = h;
    rtex->flags &= ~render::texture_flags::NEEDS_UPLOAD;
    rtex->flags |= render::texture_flags::UPLOADED;
    rtex->on_delete = _texture_destroy;
    rtex->on_delete_uptr = this;

    return true;
}


bool texture_manager::depth_target_setup(SDL_GPUDevice *gdev, rtexture *rtex,
            render::texture_format native_fmt, int w, int h, bool needs_sampling)
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

    auto gfmt = get_gpu_format(native_fmt);

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

    // create new texture block
    texture_block *new_blk = new texture_block{};
    new_blk->gtex = gtex;
    new_blk->gfmt = gfmt;
    new_blk->owns_texture = true;
    new_blk->w = w;
    new_blk->h = h;
    new_blk->type = SDL_GPU_TEXTURETYPE_2D;
    new_blk->layer_count_or_depth = 1;
    new_blk->num_levels = 1;

    rtex->rptr = new_blk;
    rtex->width = w;
    rtex->height = h;
    rtex->flags &= ~render::texture_flags::NEEDS_UPLOAD;
    rtex->flags |= render::texture_flags::UPLOADED;
    rtex->on_delete = _texture_destroy;
    rtex->on_delete_uptr = this;

    return true;

}


SDL_GPUTextureFormat texture_manager::get_gpu_format(render::texture_format fmt)
{
    // these formats are fixed
    if(fmt == render::texture_format::COLOR_RGBA_8888)
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    if(fmt == render::texture_format::COLOR_BGRA_8888)
        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    if(fmt == render::texture_format::COLOR_RGBA_8888_SRGB)
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    if(fmt == render::texture_format::COLOR_BGRA_8888_SRGB)
        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    if(fmt == render::texture_format::COLOR_R_8_UNORM)
        return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    if(fmt == render::texture_format::COLOR_R_16_FLOAT)
        return SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
    if(fmt == render::texture_format::COLOR_RG_16_FLOAT)
        return SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
    if(fmt == render::texture_format::COLOR_RGBA_16_FLOAT)
        return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    if(fmt == render::texture_format::DEPTH_16)
        return SDL_GPU_TEXTUREFORMAT_D16_UNORM;

    // TODO try to match texture format according to support

    return SDL_GPU_TEXTUREFORMAT_INVALID;
}


render::texture_format texture_manager::get_native_format(SDL_GPUTextureFormat fmt)
{
    // these format mappings are fixed
    if(fmt == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
        return render::texture_format::COLOR_RGBA_8888;
    if(fmt == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM)
        return render::texture_format::COLOR_BGRA_8888;
    if(fmt == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB)
        return render::texture_format::COLOR_RGBA_8888_SRGB;
    if(fmt == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB)
        return render::texture_format::COLOR_BGRA_8888_SRGB;
    if(fmt == SDL_GPU_TEXTUREFORMAT_R8_UNORM)
        return render::texture_format::COLOR_R_8_UNORM;
    if(fmt == SDL_GPU_TEXTUREFORMAT_R16_FLOAT)
        return render::texture_format::COLOR_R_16_FLOAT;
    if(fmt == SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT)
        return render::texture_format::COLOR_RG_16_FLOAT;
    if(fmt == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT)
        return render::texture_format::COLOR_RGBA_16_FLOAT;
    if(fmt == SDL_GPU_TEXTUREFORMAT_D16_UNORM)
        return render::texture_format::DEPTH_16;

    // TODO try to match texture format according to support

    return render::texture_format::INVALID;

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

    if(curr->owns_texture)
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
