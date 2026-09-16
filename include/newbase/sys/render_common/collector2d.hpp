#pragma once

#include <newbase/sys/render_common/batcher2d.hpp>
#include <newbase/layer.hpp>
#include <newbase/scene.hpp>

namespace nb::render 
{

    /**
     * Our 2d geometry preparation engine
     * It takes a scene registry, a render layer, and generates the 2D geometry and drawing commands into a batcher2d.
     * It should go over all the components that result in 2d rendering.
     *
     * Opaque<=>Transparent fillrate optimization could also be supported (opaque front-to-back, then
     * transparent back-to-front), using early Z, but now we also need support SDL_Renderer, that has no
     * z-buffer, so let's leave that for later. Most 2D rendering is blended anyway.
     */
    class collector2d final
    {
        public:
            /**
             * Clears any internal data that may be used in-between render passes.
             */
            void clear();

            /**
             * Performs the geometry transformation and collection passes into the batcher buffers.
             * bounds_min and bounds_max can be used to produce absolute pixel coordinates, NDC-space coordinates (-1..1),
             * and anything inbetween, including flipping.
             * @param target The batcher2d where all the geometry will be sent to.
             * @param scene The scene to collect render data from.
             * @param layer The render layer to use for viewport, camera, and layer mask information.
             * @param bounds_tl Position where the top-left coordinate will be mapped to.
             * @param bounds_tr Position where the bottom-right coordinates will be mapped to.
             */
            void collect(batcher2d &target, const scene& scene, const render_layer& layer, glm::vec2 bounds_tl, glm::vec2 bounds_tr);
    };
}
