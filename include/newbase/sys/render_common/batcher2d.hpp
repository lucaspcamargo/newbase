#pragma once

#include <newbase/utility/glm.hpp>
#include <newbase/res/fwd.hpp>
#include <unordered_map>
#include <vector>
#include <memory>

/*
    Our 2D batcher implementation

    Coalesces the engine's 2D rendering into a list of drawing operations,
    to be performed by the renderer, and buffer of associated data.

    Geometry is gathered in a flat, reusable vertex geometry buffer, with an 
    accompanying index buffer.

    The command stream describes the draw calls to be issued by the renderer.
    Each command provides spans of the index buffer to use, and the base vertex index.

    In the future, we should have our own ImGui backend that uses this directly,
    mostly for reducing duplicate code and to stop using the sample backend code.

    Opaque<=>Transparent fillrate optimization could also be supported (opaque front-to-back 
    + transparent back-to-front), but we need to also support SDL_Renderer, that has no
    z-buffer, so let's leave that for later. Most 2D rendering is blended anyway.
*/


namespace nb::render {

    /**
     * Blend mode to use in drawing commands.
     * We keep compatibility here with SDL_BlendMode for simplicity.
     */
    enum class blendmode2d : uint32_t
    {
        NONE    = 0x0,
        BLEND   = 0x1,
        ADD     = 0x2,
        MOD     = 0x4,
        MUL     = 0x8,
        INVALID = 0x7FFFFFFF
    };

    /**
     * This is the data for a single vertex to be rendered.
     * Even with glm's alignment requirements, it should be
     * 1:1 memory-compatible with SDL_Vertex.
     */
    struct vertex2d
    {
        glm::vec2 pos;
        glm::vec4 color;
        glm::vec2 uv;
    };

    /**
     * These are the flat memory buffers we'll use (and reuse)
     * for our batching operations. We also keep references to
     * textures here, so that we may reuse them.
     * 
     * tex_set is used to understand whether the texture is
     * already referenced by the drawing data and what is its index.
     * 
     * But we keep the shared_ptr's in a flat vector to avoid 
     * moving them around too much.
     */
    struct data2d
    {
        std::vector<vertex2d> verts;
        std::vector<uint16_t> inds;

        // ordered texture set, for quick indexing and reuse :)
        std::vector<std::shared_ptr<rtexture>> tex;
        std::unordered_map<rtexture*, size_t> tex_set;
    };

    /**
     * This is a single drawing command, with a base vertex index (for data2d::verts),
     * and a span of indices to render (for data2d::inds).
     * The texture index
     */
    struct command2d
    {
        int32_t base_vertex {-1};
        int32_t index_start {-1};
        uint32_t index_count {0};
        int32_t texture {-1};
        blendmode2d blend {blendmode2d::NONE};
    };

    /**
     * This is our batcher class. We use a final implementation with no virtual
     * dispatch for best performance. We also use rule-of-zero for simplicity.
     */
    class batcher2d final
    {
    public:
        void clear();
        void add_quad(); // TODO
        void add_geom(); // TODO

    private:
        data2d data;
        std::vector<command2d> comms;
    };


    // TODO a class that takes a render layer and a camera and such, 
    // goes over a scene registry, and feeds all geometry to the batcher
    // Basically we need to move code from render_simple to it
    // Then make render_simple use it, as a first user.
    // After all that, we go back to render_gpu and make it work properly too. 
};