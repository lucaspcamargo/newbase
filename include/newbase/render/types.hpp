#pragma once

#include <newbase/sdl/rect_ops.hpp>
#include <SDL3/SDL_rect.h>
#include <cmath>
#include <cstdint>

// Some types used in rendering-related structures,
// especially enums

struct SDL_Rect; // fwd

namespace nb::render
{
    /**
     * Blend mode to use in drawing commands.
     * Existing values must be compatible with SDL_BlendMode for simplicity
     */
    enum class blendmode : uint8_t
    {
        NONE    = 0x00,
        BLEND   = 0x01,
        ADD     = 0x02,
        MOD     = 0x04,
        MUL     = 0x08,
        INVALID = 0xFF
    };

    /**
     * Culling mode to use in drawing commands.
     */
    enum class cull_mode : uint8_t
    {
        NONE    = 0x00,
        BACK    = 0x01,
        FRONT   = 0x02,
        INVALID = 0xFF
    };

    /**
     * These are parameters that are intrinsic the the fixed function
     * part of the pipeline, so they are present for every material type.
     * Mostly rasterization control parameters.
     */
    struct alignas(1) pipeline_params
    {
        blendmode blend {blendmode::NONE};
        cull_mode cull {cull_mode::NONE};
        bool depth_test {true};
        bool depth_write {true};
    };

    /**
     * Our viewport type
     * A generic POD type for specifying a viewport
     * Can be safely copied and moved around
     * Memory-compatible with SDL_Rect
     */
    struct viewport_t
    {
        int x {-1};
        int y {-1};
        int w {-1};
        int h {-1};

        operator SDL_Rect&() {return *reinterpret_cast<SDL_Rect*>(this);}
        bool operator == (const viewport_t& other) const = default;
    };

    /**
     * A default viewport setup
     * When a viewport has these values, it covers the entire render target
     * A viewport that has these dimensions is not invalid
     */
    static constexpr viewport_t VIEWPORT_DEFAULT = viewport_t {};



    // CLIPPING

    /**
     * Our clip definition. Adjacent to viewports of course.
     * We just reuse SDL_FRect for simplicity.
     * For batching, this data is essentially opaque, and should just be passed through.
     * The renderer and collector define its exact meaning, apart from CLIP_NONE.
     */
    using clip_t = SDL_FRect;

    /**
     * Sentinel value used to specify no clipping
     */
    static constexpr clip_t CLIP_NONE = {-1.f, -1.f, -1.f, -1.f};

    /**
     * Empty clip, used by intersection operation
     */
    static constexpr clip_t CLIP_EMPTY = {0.f, 0.f, 0.f, 0.f};

    /**
     * A clip intersection operator that respects CLIP_NONE.
     * In case there is no intersection, returns CLIP_EMPTY.
     */
    inline clip_t clip_intersect(const clip_t&a, const clip_t&b)
    {
        if(a == CLIP_NONE)
            return b;
        else if(b == CLIP_NONE)
            return a;

        SDL_FRect ret;
        if(!SDL_GetRectIntersectionFloat(&a, &b, &ret))
            return CLIP_EMPTY;

        return ret;
    }

    /**
     * Converts clip to integer coordinates
     * It rounds numbers to consider pixel coverage, instead of flooring
     * Must NOT be CLIP_NONE by definition, but could be CLIP_EMPTY (still degenerate)
     */
    inline SDL_Rect clip_to_int(const clip_t& clip)
    {
        const int x1 = static_cast<int>(std::round(clip.x));
        const int y1 = static_cast<int>(std::round(clip.y));
        const int x2 = static_cast<int>(std::round(clip.x + clip.w));
        const int y2 = static_cast<int>(std::round(clip.y + clip.h));

        return SDL_Rect {
            x1,
            y1,
            x2 - x1,
            y2 - y1
        };
    }


    // RENDER TARGET TYPES
    // Mostly managed internally by the renderer
    // Here the concept of a "default target" is useful, meaning the main
    // window swapchain.
    // For a friendly RAII interface, see ::nb::render::target_ref (target.hpp)
    // The opaque handle is defined here for header decoupling

    /// Type definition for a render target identifier.
    using target_id_t = uint32_t;

    /// Default render target identifier, meaning the main window.
    static constexpr target_id_t TARGET_DEFAULT = UINT32_MAX;

    /// An invalid render target
    /// Returned by the renderer when the target description is malformed,
    /// or the target could not be built for some reason.
    static constexpr target_id_t TARGET_INVALID = 0;

    /// Render target sizing policy.
    enum class target_size_mode {
        /// Use width and height in description.
        ABSOLUTE,

        /// Size is in relation to main window viewport, scaled by factor.
        UI_RELATIVE,

        /// Size is in relation to another render target, scaled by factor.
        /// This implies sizing dependencies inbetween targets, so renderer
        /// must ideally check for cyclic dependencies on target creation.
        TARGET_RELATIVE
    };

    /// Descriptor object for a render target.
    /// Used in render target creation (via the renderer service).
    struct target_desc
    {
        target_size_mode size_mode{target_size_mode::ABSOLUTE};
        unsigned int abs_width {0};                 // for absolute
        unsigned int abs_height {0};                // for absolute
        float size_scale {1.0f};                    // for relative
        target_id_t size_source {TARGET_INVALID};   // for target relative
        bool has_depth {false};
        bool init_clear {false};
    };



    // Texture sampling types

    enum class sampler_filter : uint8_t
    {
        NEAREST,
        LINEAR
    };

    enum class sampler_addressing : uint8_t
    {
        REPEAT,
        REPEAT_MIRRORED,
        CLAMP_TO_EDGE
    };

    struct sampler_config
    {
        sampler_filter min_filter {sampler_filter::NEAREST};
        sampler_filter mag_filter {sampler_filter::NEAREST};
        sampler_filter mip_filter {sampler_filter::NEAREST};
        sampler_addressing addr_mode_u {sampler_addressing::REPEAT};
        sampler_addressing addr_mode_v {sampler_addressing::REPEAT};
        sampler_addressing addr_mode_w {sampler_addressing::REPEAT};
        float max_aniso {1.0f};
        float max_lod {1000.0f}; // set to 0 to disable mipmapping

        bool operator == (const sampler_config &other) const
        {
            return min_filter == other.min_filter
                && mag_filter == other.mag_filter
                && mip_filter == other.mip_filter
                && addr_mode_u == other.addr_mode_u
                && addr_mode_v == other.addr_mode_v
                && addr_mode_w == other.addr_mode_w
                && max_aniso == other.max_aniso
                && max_lod   == other.max_lod;
        }
    };


    // Material-related general types


    /// provided by the material controller to denote shader variants according
    /// to material parameters
    /// directly added to pipeline descriptors to avoid hash collisions
    /// use 1:1 with shader set that the the concrete controller may provide
    /// to avoid unnecessary pipeline states
    using material_variant = uint16_t;   // 64ksv should be enough for everyone

    /// type of material known to the engine
    /// maps directly to a material controller type
    enum class material_type : uint8_t
    {
        INVALID,
        STANDARD_2D,
        STANDARD_PBR,
        DYNAMIC  // TODO for custom/scriptable material types in the future
    };

};

