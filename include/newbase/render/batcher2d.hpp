#pragma once

#include <newbase/render/types.hpp>
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

    Also used by our custom ImGui backend. So all our 2D rendering goes through here.
*/


namespace nb::render {

    /**
     * This is the data for a single vertex to be rendered.
     * Even with glm's alignment requirements, it should be
     * 1:1 memory-compatible with SDL_Vertex.
     *
     * Using uint32_t for color was considered, but since we
     * would need to convert back to float for SDL_Renderer
     * all the same, it is what it is.
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
     *
     * You may want to call reserve() on the vectors with some
     * nice defaults, or keep the data buffers around per draw
     * layer so that they stabilize.
     */
    struct data2d
    {
        std::vector<vertex2d> verts;
        std::vector<uint16_t> inds;

        // ordered texture set, for quick indexing and reuse :)
        // we keep the shared_ptr references on a flat structure for speed,
        // while also keeping an unordered map for quick index lookup by value
        std::vector<std::shared_ptr<rtexture>> tex;
        std::unordered_map<rtexture*, size_t> tex_set;

        inline void clear()
        {
            // clear data buffers. research shows
            // this preserves vector capacities,
            // so should be ok to call every frame
            inds.clear();
            verts.clear();
            tex_set.clear();
            tex.clear();
        }
    };

    /**
     * This is a single drawing command, with a base vertex index (for data2d::verts),
     * and a span of indices to render (for data2d::inds).
     * The texture index can be -1 for "no texture".
     * clip is used to store a reference to a clipping region.
     * The clip data is opaque and renderer-dependant.
     */
    struct command2d
    {
        uint32_t base_vertex {0};
        uint32_t index_start {0};
        uint32_t index_count {0};
        uint32_t vtx_count {0};
        int32_t texture {-1};
        blendmode blend {blendmode::NONE};
        clip_t clip {CLIP_NONE};
    };

    /**
     * This is our batcher class. We use a final implementation with no virtual
     * dispatch for best performance. We also use rule-of-zero for simplicity.
     *
     * TODO as a future optimization, we may add a mechanism to allow for direct
     *      data writes by the users. This should follow a API protocol that gives
     *      the users references to the buffers, and whether indices
     *      have to be written with an offset.
     *
     *      This would avoid the need for the extra copy.
     */
    class batcher2d final
    {
    public:
        /**
         * Clears the internal data buffers.
         */
        void clear();

        /**
         * Adds geometry to the data buffers, and a drawing command with the given texture and blendmode.
         * May append to the previous drawing command if the texture and blendmode are the same.
         * Other optimizations may be implemented in the future.
         */
        void add_geom(const vertex2d *verts, uint32_t vcount, const uint16_t *inds, uint32_t icount,
                      std::shared_ptr<rtexture> tex = nullptr, blendmode blend = blendmode::NONE, clip_t clip = CLIP_NONE);

        /// const ref getter for draw data
        const data2d & data() const { return m_data; }

        /// const ref getter for draw commands
        const std::vector<command2d>& commands()const { return m_comms; }

    private:
        data2d m_data {};
        std::vector<command2d> m_comms {};
    };
}
