#include "newbase/res/resource.hpp"
#include <newbase/res/texture.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>

#include <entt/core/hashed_string.hpp>
#include <SDL3/SDL_pixels.h>
#include <stb_image.h>
#include <tiny_ktx/tinyktx.h>

#include <fstream>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio> // or <format> in C++20
#include <cstring>
#include <limits>
#include <cstdlib>
#include <utility>

void save_vector_to_file(uint32_t id, const uint8_t *data, size_t len) {
    // 1. Format the filename
    char filename[32];
    std::snprintf(filename, sizeof(filename), "0x%08x.bin", id);

    // 2. Open file in binary mode and write raw buffer
    std::ofstream file(filename, std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(data), len);
    }
}

using namespace nb;
using entt::operator""_hs;

namespace {

constexpr uint8_t ktx1_identifier[] = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB,
    0x0D, 0x0A, 0x1A, 0x0A
};
constexpr uint8_t ktx2_identifier[] = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB,
    0x0D, 0x0A, 0x1A, 0x0A
};

render::texture_format map_ktx_format(TinyKtx_Format format)
{
    switch(format)
    {
        case TKTX_R8_UNORM: return render::texture_format::COLOR_R_8_UNORM;
        case TKTX_R8G8B8A8_UNORM: return render::texture_format::COLOR_RGBA_8888;
        case TKTX_B8G8R8A8_UNORM: return render::texture_format::COLOR_BGRA_8888;
        case TKTX_R8G8B8A8_SRGB: return render::texture_format::COLOR_RGBA_8888_SRGB;
        case TKTX_B8G8R8A8_SRGB: return render::texture_format::COLOR_BGRA_8888_SRGB;
        case TKTX_R16_SFLOAT: return render::texture_format::COLOR_R_16_FLOAT;
        case TKTX_R16G16_SFLOAT: return render::texture_format::COLOR_RG_16_FLOAT;
        case TKTX_R16G16B16A16_SFLOAT: return render::texture_format::COLOR_RGBA_16_FLOAT;
        default: return render::texture_format::INVALID;
    }
}

uint32_t bytes_per_pixel(render::texture_format format)
{
    switch(format)
    {
        case render::texture_format::COLOR_R_8_UNORM: return 1;
        case render::texture_format::COLOR_R_16_FLOAT: return 2;
        case render::texture_format::COLOR_RG_16_FLOAT:
        case render::texture_format::COLOR_RGBA_8888:
        case render::texture_format::COLOR_BGRA_8888:
        case render::texture_format::COLOR_RGBA_8888_SRGB:
        case render::texture_format::COLOR_BGRA_8888_SRGB: return 4;
        case render::texture_format::COLOR_RGBA_16_FLOAT: return 8;
        default: return 0;
    }
}

struct ktx_memory_reader
{
    const uint8_t *data;
    size_t size;
    size_t position {0};
};

void *ktx_alloc(void *, size_t size) { return std::malloc(size); }
void ktx_free(void *, void *memory) { std::free(memory); }
void ktx_error(void *, const char *message) { log::error("[rtexture] KTX1: %s", message); }
size_t ktx_read(void *user, void *buffer, size_t size)
{
    auto &reader = *static_cast<ktx_memory_reader *>(user);
    const size_t count = std::min(size, reader.size - reader.position);
    std::memcpy(buffer, reader.data + reader.position, count);
    reader.position += count;
    return count;
}
bool ktx_seek(void *user, int64_t offset)
{
    auto &reader = *static_cast<ktx_memory_reader *>(user);
    if(offset < 0 || static_cast<uint64_t>(offset) > reader.size)
        return false;
    reader.position = static_cast<size_t>(offset);
    return true;
}
int64_t ktx_tell(void *user)
{
    return static_cast<int64_t>(static_cast<ktx_memory_reader *>(user)->position);
}

uint32_t read_u32_le(const uint8_t *p);

bool checked_multiply(uint64_t &value, uint64_t factor)
{
    if(factor && value > std::numeric_limits<uint64_t>::max() / factor)
        return false;
    value *= factor;
    return true;
}

bool load_ktx1(rtexture &texture, const uint8_t *bytes, size_t size)
{
    constexpr size_t header_size = 64;
    if(size < header_size || bytes[12] != 0x01 || bytes[13] != 0x02 ||
       bytes[14] != 0x03 || bytes[15] != 0x04)
    {
        log::error("[rtexture] KTX1: truncated header or big-endian data is unsupported");
        return false;
    }

    const uint32_t pixel_width = read_u32_le(bytes + 36);
    const uint32_t pixel_height = read_u32_le(bytes + 40);
    const uint32_t pixel_depth = read_u32_le(bytes + 44);
    const uint32_t array_count = read_u32_le(bytes + 48);
    const uint32_t face_count = read_u32_le(bytes + 52);
    const uint32_t declared_mip_count = read_u32_le(bytes + 56);
    const uint32_t key_value_size = read_u32_le(bytes + 60);
    const uint32_t mip_count = declared_mip_count ? declared_mip_count : 1;
    if(!pixel_width || array_count != 0 || (face_count != 1 && face_count != 6) ||
       (pixel_depth && (pixel_height == 0 || array_count != 0 || face_count != 1)) ||
       (face_count == 6 && (!pixel_height || pixel_width != pixel_height)) ||
       key_value_size > size - header_size || mip_count > TINYKTX_MAX_MIPMAPLEVELS)
    {
        log::error("[rtexture] KTX1: invalid dimensions, metadata, or unsupported array texture");
        return false;
    }
    uint32_t full_mip_count = 1;
    for(uint32_t dimension = std::max({pixel_width, pixel_height, pixel_depth});
        dimension > 1; dimension >>= 1)
        ++full_mip_count;
    if(mip_count > full_mip_count)
    {
        log::error("[rtexture] KTX1: mip count exceeds the dimension chain");
        return false;
    }

    // TinyKtx's metadata reader is useful here, but its mip helper omits array images.
    ktx_memory_reader reader {bytes, size};
    TinyKtx_Callbacks callbacks {};
    callbacks.errorFn = ktx_error;
    callbacks.allocFn = ktx_alloc;
    callbacks.freeFn = ktx_free;
    callbacks.readFn = ktx_read;
    callbacks.seekFn = ktx_seek;
    callbacks.tellFn = ktx_tell;
    auto context = TinyKtx_CreateContext(&callbacks, &reader);
    if(!context)
        return false;

    const bool parsed = TinyKtx_ReadHeader(context);
    bool loaded = false;
    if(parsed && !TinyKtx_NeedsEndianCorrecting(context))
    {
        const auto format = map_ktx_format(TinyKtx_GetFormat(context));
        if(format != render::texture_format::INVALID)
        {
            const uint32_t width = pixel_width;
            const uint32_t height = std::max(1u, pixel_height);
            const uint32_t depth = std::max(1u, pixel_depth);
            const uint64_t layer_count = uint64_t(array_count ? array_count : 1) * face_count;
            const uint32_t mip_count = TinyKtx_NumberOfMipmaps(context);
            if(!bytes_per_pixel(format) || !mip_count ||
               layer_count > std::numeric_limits<uint32_t>::max())
            {
                TinyKtx_DestroyContext(context);
                return false;
            }

            texture.type = pixel_depth ? render::texture_type::IMAGE_3D :
                           face_count == 6 ? render::texture_type::CUBEMAP :
                           render::texture_type::IMAGE_2D;
            texture.format = format;
            texture.width = width;
            texture.height = height;
            texture.depth = depth;
            texture.layer_count = static_cast<uint32_t>(layer_count);
            texture.data.clear();
            texture.mipmaps.clear();

            loaded = true;
            for(uint32_t level = 0; level < mip_count; ++level)
            {
                const uint32_t level_size = TinyKtx_ImageSize(context, level);
                const auto *level_data =
                    static_cast<const uint8_t *>(TinyKtx_ImageRawData(context, level));
                if(!level_size || !level_data ||
                   level_size > std::numeric_limits<size_t>::max() - texture.data.size())
                {
                    loaded = false;
                    break;
                }
                const size_t data_offset = texture.data.size();
                texture.data.insert(texture.data.end(), level_data, level_data + level_size);
                texture.mipmaps.push_back({data_offset, level_size});
            }
            if(loaded && declared_mip_count == 0)
                texture.flags |= render::texture_flags::GENERATE_MIPMAPS;
        }
    }
    TinyKtx_DestroyContext(context);
    return loaded;
}

uint32_t read_u32_le(const uint8_t *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint64_t read_u64_le(const uint8_t *p)
{
    return uint64_t(read_u32_le(p)) | (uint64_t(read_u32_le(p + 4)) << 32);
}

bool load_ktx2(rtexture &texture, const uint8_t *bytes, size_t size)
{
    // tinyktx2 reads 80 bytes per mip entry although entries are 24 bytes, so parse the index here.
    constexpr size_t header_size = 80;
    constexpr size_t level_index_entry_size = 24;
    if(size < header_size || std::memcmp(bytes, ktx2_identifier, sizeof(ktx2_identifier)) != 0)
    {
        log::warn("[rtexture] KTX2: truncated header or invalid identifier");
        return false;
    }

    const uint32_t vk_format = read_u32_le(bytes + 12);
    const uint32_t pixel_width = read_u32_le(bytes + 20);
    const uint32_t pixel_height = read_u32_le(bytes + 24);
    const uint32_t pixel_depth = read_u32_le(bytes + 28);
    const uint32_t array_count = read_u32_le(bytes + 32);
    const uint32_t face_count = read_u32_le(bytes + 36);
    const uint32_t declared_level_count = read_u32_le(bytes + 40);
    const uint32_t level_count = std::max(1u, declared_level_count);
    const uint32_t supercompression = read_u32_le(bytes + 44);
    const uint32_t type_size = read_u32_le(bytes + 16);
    const uint32_t dfd_offset = read_u32_le(bytes + 48);
    const uint32_t dfd_length = read_u32_le(bytes + 52);
    const uint32_t kvd_offset = read_u32_le(bytes + 56);
    const uint32_t kvd_length = read_u32_le(bytes + 60);
    const uint64_t sgd_offset = read_u64_le(bytes + 64);
    const uint64_t sgd_length = read_u64_le(bytes + 72);

    const auto format = map_ktx_format(static_cast<TinyKtx_Format>(vk_format));
    if(format == render::texture_format::INVALID || supercompression != 0 ||
       !pixel_width || (face_count != 1 && face_count != 6) ||
       (pixel_depth && (pixel_height == 0 || array_count != 0 || face_count != 1)) ||
       (face_count == 6 && (!pixel_height || pixel_width != pixel_height)) ||
       level_count > TINYKTX_MAX_MIPMAPLEVELS ||
       level_count > (size - header_size) / level_index_entry_size)
    {
        log::error("[rtexture] KTX2: unsupported format, compression, or dimensions");
        return false;
    }

    const uint32_t pixel_size = bytes_per_pixel(format);
    if(!pixel_size)
    {
        log::warn("[rtexture] KTX2: unsupported pixel size for vkFormat %u", vk_format);
        return false;
    }
    const uint32_t expected_type_size =
        format == render::texture_format::COLOR_R_16_FLOAT ||
        format == render::texture_format::COLOR_RG_16_FLOAT ||
        format == render::texture_format::COLOR_RGBA_16_FLOAT ? 2 : 1;
    if(type_size != expected_type_size)
    {
        log::warn("[rtexture] KTX2: invalid typeSize %u for vkFormat %u (expected %u)",
                  type_size, vk_format, expected_type_size);
        return false;
    }

    uint32_t full_mip_count = 1;
    for(uint32_t dimension = std::max({pixel_width, pixel_height, pixel_depth});
        dimension > 1; dimension >>= 1)
        ++full_mip_count;
    if(level_count > full_mip_count)
    {
        log::error("[rtexture] KTX2: mip count exceeds the dimension chain");
        return false;
    }

    const uint64_t level_index_end = header_size + uint64_t(level_count) * level_index_entry_size;
    uint64_t metadata_end = level_index_end;
    const auto extend_metadata_end = [&](uint64_t offset, uint64_t length) {
        if(length == 0)
            return true;
        if(offset < level_index_end || offset > size || length > size - offset)
            return false;
        metadata_end = std::max(metadata_end, offset + length);
        return true;
    };
    if(!extend_metadata_end(dfd_offset, dfd_length) ||
       !extend_metadata_end(kvd_offset, kvd_length) ||
       !extend_metadata_end(sgd_offset, sgd_length))
    {
        log::warn("[rtexture] KTX2: invalid DFD, KVD, or SGD metadata range");
        return false;
    }

    const uint32_t width = pixel_width;
    const uint32_t height = std::max(1u, pixel_height);
    const uint32_t depth = std::max(1u, pixel_depth);
    const uint64_t layer_count = uint64_t(array_count ? array_count : 1) * face_count;
    if(layer_count > std::numeric_limits<uint32_t>::max())
    {
        log::error("[rtexture] KTX2: layer count is too large");
        return false;
    }
    const uint32_t layers = static_cast<uint32_t>(layer_count);
    texture.type = pixel_depth ? render::texture_type::IMAGE_3D :
                   face_count == 6 ? render::texture_type::CUBEMAP :
                   render::texture_type::IMAGE_2D;
    texture.format = format;
    texture.width = width;
    texture.height = height;
    texture.depth = depth;
    texture.layer_count = layers;
    texture.data.clear();
    texture.mipmaps.clear();

    std::vector<std::pair<uint64_t, uint64_t>> level_ranges;
    for(uint32_t level = 0; level < level_count; ++level)
    {
        const uint8_t *entry = bytes + header_size + level * level_index_entry_size;
        const uint64_t offset = read_u64_le(entry);
        const uint64_t byte_length = read_u64_le(entry + 8);
        const uint64_t uncompressed_length = read_u64_le(entry + 16);
        const uint64_t mip_width = std::max(1u, width >> level);
        const uint64_t mip_height = std::max(1u, height >> level);
        const uint64_t mip_depth = std::max(1u, depth >> level);
        uint64_t expected_size = pixel_size;
        bool size_overflow = false;
        for(const uint64_t dimension : {mip_width, mip_height, mip_depth, uint64_t(layers)})
        {
            if(dimension && expected_size > std::numeric_limits<uint64_t>::max() / dimension)
            {
                size_overflow = true;
                break;
            }
            expected_size *= dimension;
        }
        if(size_overflow || offset < metadata_end ||
           byte_length != uncompressed_length || byte_length != expected_size ||
           offset > size || byte_length > size - offset ||
           byte_length > std::numeric_limits<size_t>::max() - texture.data.size())
        {
            texture.data.clear();
            texture.mipmaps.clear();
            log::error("[rtexture] KTX2: invalid mip level payload");
            return false;
        }
        const uint64_t level_end = offset + byte_length;
        for(const auto &[existing_start, existing_end] : level_ranges)
        {
            if(offset < existing_end && existing_start < level_end)
            {
                texture.data.clear();
                texture.mipmaps.clear();
                log::error("[rtexture] KTX2: overlapping mip payloads");
                return false;
            }
        }
        level_ranges.emplace_back(offset, level_end);
        const size_t data_offset = texture.data.size();
        texture.data.insert(texture.data.end(), bytes + offset, bytes + offset + byte_length);
        texture.mipmaps.push_back({data_offset, static_cast<size_t>(byte_length)});
    }
    if(declared_level_count == 0)
        texture.flags |= render::texture_flags::GENERATE_MIPMAPS;
    return true;
}

}


static SDL_Surface* load_texture_surface_from_data(entt::id_type id, const uint8_t *data, size_t len)
{
    if(len > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        log::error("[rtexture] surface: image data is too large: %x", id);
        return nullptr;
    }
    int w, h, chs;
    auto ptr = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data),
                                     static_cast<int>(len), &w, &h, &chs, 4);
    if (!ptr)
    {
        log::error("[rtexture] surface: stbi decode failed: %x", id);
        save_vector_to_file(id, data, len);
        return nullptr;
    }
    log::info("[rtexture] surface: loaded: %dx%d, %dchs", w, h, chs);
    if(w <= 0 || h <= 0 || w > std::numeric_limits<int>::max() / 4)
    {
        stbi_image_free(ptr);
        return nullptr;
    }
    SDL_Surface* tmp = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, ptr, w * 4);
    if(!tmp)
    {
        stbi_image_free(ptr);
        log::error("[rtexture] surface: SDL surface creation failed: %s", SDL_GetError());
        return nullptr;
    }
    SDL_Surface* surf = SDL_DuplicateSurface(tmp);
    SDL_DestroySurface(tmp);
    stbi_image_free(ptr);
    return surf;
}

// Standalone surface loader: reads asset bytes via the resource manager and decodes
// them with stb_image.  Returns a caller-owned SDL_Surface*, or nullptr on failure.
static SDL_Surface* load_texture_surface(entt::id_type id)
{
    std::vector<char> data;
    if (!rman().read_all_sync(id, data))
    {
        log::error("[rtexture] surface: data loading failed: %x", id);
        return nullptr;
    }
    SDL_Surface* surf = load_texture_surface_from_data(id, (const uint8_t*) data.data(), data.size());
    return surf;
}


rtexture::~rtexture()
{
    if(on_delete)
        on_delete(*this, on_delete_uptr);
    if(surf)
        SDL_DestroySurface(surf);
}


// texture loader
bool rtexture::do_load()
{
    log::info("[rtexture] loading: %x", id());

    std::vector<uint8_t> embedded_data;
    bool has_data = false;
    auto hnd_it = rman().handles().find(id());
    if(hnd_it != rman().handles().end())
    {
        const auto &hnd = hnd_it->second;
        if(hnd.parent_id != 0)
        {
            auto parent = rman().load_sync("rgltf"_hs, hnd.parent_id);
            if(parent)
            {
                has_data = parent->get_subresource_data(id(), embedded_data) && !embedded_data.empty();
            }
        }
    }

    std::vector<char> asset_data;
    const uint8_t *bytes = nullptr;
    size_t size = 0;
    if(has_data)
    {
        bytes = embedded_data.data();
        size = embedded_data.size();
    }
    else
    {
        if(!rman().read_all_sync(id(), asset_data) || asset_data.empty())
        {
            log::error("[rtexture] data loading failed: %x", id());
            return false;
        }
        bytes = reinterpret_cast<const uint8_t *>(asset_data.data());
        size = asset_data.size();
    }

    const bool is_ktx1 = size >= sizeof(ktx1_identifier) &&
                         std::memcmp(bytes, ktx1_identifier, sizeof(ktx1_identifier)) == 0;
    const bool is_ktx2 = size >= sizeof(ktx2_identifier) &&
                         std::memcmp(bytes, ktx2_identifier, sizeof(ktx2_identifier)) == 0;
    bool loaded = false;
    surf = nullptr;
    data.clear();
    mipmaps.clear();
    flags &= ~(render::texture_flags::SDL_SURFACE |
               render::texture_flags::GENERATE_MIPMAPS);
    type = render::texture_type::IMAGE_2D;
    format = render::texture_format::INVALID;
    width = height = 0;
    depth = layer_count = 1;
    if(is_ktx1)
    {
        loaded = load_ktx1(*this, bytes, size);
    }
    else if(is_ktx2)
    {
        loaded = load_ktx2(*this, bytes, size);
    }
    else
    {
        surf = load_texture_surface_from_data(id(), bytes, size);
        loaded = surf != nullptr;
        if(loaded)
        {
            type = render::texture_type::IMAGE_2D;
            format = render::texture_format::COLOR_RGBA_8888;
            width = static_cast<uint32_t>(surf->w);
            height = static_cast<uint32_t>(surf->h);
            depth = layer_count = 1;
            flags |= render::texture_flags::GENERATE_MIPMAPS;
        }
    }

    if(!loaded)
    {
        if(surf)
        {
            SDL_DestroySurface(surf);
            surf = nullptr;
        }
        data.clear();
        mipmaps.clear();
        format = render::texture_format::INVALID;
        width = height = 0;
        depth = layer_count = 1;
        log::error("[rtexture] texture data unsupported or invalid: %x", id());
        return false;
    }

    rptr            = nullptr;
    flags |= render::texture_flags::NEEDS_UPLOAD;
    flags &= ~render::texture_flags::UPLOADED;
    if(surf)
    {
        flags |= render::texture_flags::SDL_SURFACE;
        reload_surface = load_texture_surface;
    }
    else
    {
        flags &= ~render::texture_flags::SDL_SURFACE;
        reload_surface = nullptr;
    }
    return true;
}

void rtexture::load_from(SDL_Surface* s)
{
    assert(state() == resource_state::CREATED && s);
    surf = s;
    rptr            = nullptr;
    flags |= render::texture_flags::SDL_SURFACE | render::texture_flags::NEEDS_UPLOAD;
    flags |= render::texture_flags::GENERATE_MIPMAPS;
    flags &= ~render::texture_flags::UPLOADED;
    if(surf)
    {
        width  = surf->w;
        height = surf->h;
    }
    force_state(resource_state::LOADED);
}
