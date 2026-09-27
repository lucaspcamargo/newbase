#pragma once

#include <newbase/render/batcher2d.hpp>
#include <newbase/render/window.hpp>
#include <newbase/utility/mixins.hpp>
#include "SDL3/SDL_events.h"
#include <memory>

/**
 * ImGui integration for newbase
 * WIP
 * Meant to replace the sample backends.
 * Instead of using SDL_Renderer or SDL_GPU directly,
 * it uses our common 2D rendering primitives.
 *
 * The events and window integration will invariably be similar.
 */

namespace nb
{
    struct imgui_nb_p;

    class imgui_nb final : public nocopy
    {
    public:
        imgui_nb();
        ~imgui_nb();

        void init(render::window &win);

        void teardown();

        /**
         * Prepares ImGui IO information for a new frame.
         * NOTE: Does not invoke ImGui::NewFrame() by itself.
         * @param delta The elapsed time since lat frame, in seconds, or -1 for automati timing.
         */
        void new_frame(float delta = -1.0);

        /**
         * Processes an SDL event.
         * @return Whether this evet was evaluated or not.
         */
        bool event(SDL_Event *evt);

        /**
         * Goes over the ImGui drawing buffers, and collects rendering commands and data into the 2D batcher.
         * Roughtly equivalent to a standard backend's RenderDrawData, except that it does not render, it batches instead.
         * Also collects texture update data
         * NOTE: This DOES invoke ImGui::Render()
         */
        void render_flush();

        /**
         * Getter for 2d batches prepared by render_flush()
         */
        render::batcher2d& render_data();


        /**
         * Function type for texture lookup callback
         */
        using texture_getter_t = std::function<std::shared_ptr<rtexture>(uint64_t)>;

        /**
         * Setter for texture getter callback
         */
        void set_texture_lookup_callback(texture_getter_t getter);

        /**
         * Getter for main window of this backend
         */
        render::window& window() const;

    private:
        void _rebuild_font_atlas(bool force = false);

        std::unique_ptr<imgui_nb_p> _d {nullptr};
    };
}
