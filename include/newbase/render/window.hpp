#pragma once

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_events.h>
#include <ryml.hpp>

// Our window class, to be used by the renderer implementations.
// Simply builds and keeps an SDL Window, initializing it with the necessary flags and properties.
// For simpllicity, we use the SDL api in our interface.
// We also always deal in real pixel coordinates, not logical pixels.

namespace nb::render
{

class window final
{
    public:
        /**
         * Creates a new window, for the givenn render system configuration.
         * If get() return NULL after construction, it means window creation failed.
         * @param render_cfg The Yaml tree of the renderer config. Only
         *                   window-related params will be used.
         */
        window() = default;
        ~window();

        // non-copyable, non-movable
        window(const window&) = delete;
        window(window&&) = delete;
        window& operator =(const window&) = delete;
        window& operator =(window&&) = delete;

        /**
         * Creates the internal, managed window.
         * Uses the configuration data from the render system for size and other options.
         * The renderer creating this window may also pass additional flags.
         */
        bool create(ryml::ConstNodeRef render_cfg, SDL_WindowFlags extra_flags = 0);

        /**
         * Gets the raw SDL_Window pointer associated with this window object
         */
        SDL_Window * get() const {return m_win;}

        /**
         * Gets the current window width, in device pixels.
         * Events need to be passed by the renderer in order to update this.
         */
        int width() const {return m_pw;}

        /**
         * Gets the current window width, in device pixels.
         * Events need to be passed by the renderer in order to update this.
         */
        int height() const {return m_ph;}

        /**
         * Gets the current window ui scaling, as reported by the OS.
         * Events need to be passed by the renderer in order to update this.
         * Even then, ui scale changes may be flaky.
         */
        float ui_scale() const {return m_ui_scale;}

        /**
         * Get the safe area of this window.
         * Events need to be passed by the renderer in order to update this.
         */
        const SDL_Rect & safe_area() const { return m_safe_area; }

        /**
         * Processes an SDL_Event that might be related to the window
         * @returns true if the event was related to the window, and internal state was updated, false otherwise
         */
        bool event(SDL_Event * evt);

        /**
         * Shows the window and updates dimension and scale information.
         * @returns true if showing the window succeeded. On failure, you can check SDL_GetError() for details.
         */
        bool show();

private:
    SDL_Window *m_win {nullptr};
    int m_pw {0}, m_ph {0};
    float m_ui_scale {1.0};
    SDL_Rect m_safe_area;
};

};
