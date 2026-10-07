#pragma once

#include <newbase/render/batcher2d.hpp>
#include <newbase/layer.hpp>
#include <newbase/scene.hpp>

namespace nb::render 
{

    /**
     * Our 2d geometry generation engine
     * It takes a scene registry, a render layer, and generates the 2D geometry
     * and drawing commands into a batcher2d, from all the components that
     * result in 2d rendering. It can also batch all render layers at once
     * using the batcher's sync points feature.
     *
     * TODO culling, camera and BVH based. we need some sort of AABB model in cspatial.
     *
     * Opaque<=>Transparent fillrate optimization could also be supported (opaque front-to-back, then
     * transparent back-to-front), using early Z, but now we also need support SDL_Renderer, that has no
     * z-buffer and no z oordinate at all, so let's leave that for later. Most 2D rendering is blended anyway.
     *
     */
    class collector2d final
    {
        // TODO This sorts the scene multiple times, every time we collect()
        //      a layer. We can keep some state to avoid sorting the scene
        //      more than once. We could also rethink scene contents sorting
        //      entirely.
        //      Also, if we add API to batcher2d so that we can allocate and
        //      write directly to its internal buffers, we could avoid a copy
        //      and drop the internal scratch buffers.
        public:

            /**
             * Controls how multi-layer batching inserts clear quads.
             * This might be useful as regular clears do not work for multiple
             * viewports on the same render target, on many APIs
             */
            enum class clear_quad_mode
            {
                /// never add viewport clear quads
                NO_CLEAR_QUADS,
                /// always add viewport clear quads
                CLEAR_QUAD_ALWAYS,
                /* TODO for allowing regular clears on the first layer of a target
                /// adds a clear quad from the second layer of a render target onwards
                // CLEAR_QUAD_NOT_FIRST */
            };


            /**
             * Clears any internal data that may be used in-between render passes.
             */
            void clear();

            /**
             * Performs the geometry transformation and collection passes into the batcher buffers.
             * bounds_min and bounds_max can be used to produce absolute pixel coordinates, NDC-space coordinates (-1..1),
             * and anything inbetween, including flipping.
             * No sync points are added to the batcher.
             * @param target The batcher2d where all the geometry will be sent to.
             * @param scene The scene to collect render data from.
             * @param layer The render layer to use for viewport, camera, and layer mask information.
             * @param viewproj The view-projection matrix, from world space to clip space
             *
             * NOTE that the nb::scene reference is not const!
             * This is because we sort the spatial components by z-coordinate.
             */
            void collect(batcher2d &target, scene& scene, const render_layer& layer, const glm::mat4 &viewproj);


            /**
             * A struct that stores the results of global layer 2D geometry
             * collection.
             * @sa collect_all_layers
             */
            struct results
            {
                std::unordered_map<int, batcher2d::sync_id_t> sync_mapping;
            };

            /**
             * Collects 2d gometry on the same batcher, for all engine render
             * layers. A batcher sync point is generated for each layer.
             * The mapping of layer data to sync points is stored in results.
             * The viewproj used for collection maps the 2d world bounds covered
             * by the camera to the layer's 2d viewport area.
             * The batcher is not cleared beforehand. Sync points are created for
             * all layers. A final sync point is created at the end, and its id
             * is returned.
             * @sa results
             * @sa collect
             * @returns The id of the sync point created at the end.
             */
            batcher2d::sync_id_t collect_all_layers(batcher2d &target, results &results, clear_quad_mode mode = clear_quad_mode::NO_CLEAR_QUADS);

        private:
            // scratch buffers for transform operations
            std::vector<vertex2d> transform_buf;
            std::vector<uint16_t> ind_buf;
            std::vector<uint16_t> ind_buf_seq;  // used when meshes have no indices, but we must still provide them
    };
}
