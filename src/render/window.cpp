#include <newbase/render/window.hpp>
#include <newbase/log.hpp>

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_video.h"
#include <ryml_std.hpp>

using namespace nb;


bool render::window::create(ryml::ConstNodeRef cfg, SDL_WindowFlags extra_flags)
{
    // cannot recreate window with same object
    if(m_win)
        return false;

    #ifdef NEWBASE_WII
    Uint32 window_flags = 0;
    int window_w = 640, window_h = 480;
    #else
    Uint32 window_flags = extra_flags | SDL_WINDOW_RESIZABLE
    | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED;
    int window_w = 1024, window_h = 768;

    if (cfg.has_child("w") && !cfg["w"].invalid())
        cfg["w"] >> window_w;
    if (cfg.has_child("h") && !cfg["h"].invalid())
        cfg["h"] >> window_h;

    // kmsdrm has no windowing system: "window size" *is* the physical DRM
    // mode (see KMSDRM_GetClosestDisplayMode(), matched against the size
    // we ask for below), so go exclusive-fullscreen at the monitor's
    // highest available resolution/refresh rate instead of whatever
    // size was requested above.
    SDL_DisplayMode best_mode {};
    bool have_best_mode = false;
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "kmsdrm") == 0)
    {
        int mode_count = 0;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &mode_count);
        for (int i = 0; i < mode_count; i++)
        {
            const SDL_DisplayMode *m = modes[i];
            if (!have_best_mode
                || (m->w * m->h > best_mode.w * best_mode.h)
                || (m->w * m->h == best_mode.w * best_mode.h && m->refresh_rate > best_mode.refresh_rate))
            {
                best_mode = *m;
                have_best_mode = true;
            }
        }
        SDL_free(modes);

        if (have_best_mode)
        {
            window_w = best_mode.w;
            window_h = best_mode.h;
            window_flags = (window_flags & ~SDL_WINDOW_MAXIMIZED) | SDL_WINDOW_FULLSCREEN;
            log::info("[window] kmsdrm: using native mode %dx%d@%gHz", window_w, window_h, best_mode.refresh_rate);
        }
        else
            log::warn("[window] kmsdrm: SDL_GetFullscreenDisplayModes() found nothing, falling back to %dx%d", window_w, window_h);
    }
    #endif
    m_win = SDL_CreateWindow(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING), window_w, window_h, window_flags);
    if (m_win == nullptr)
    {
        log::error("[window] SDL_CreateWindow(): %s\n", SDL_GetError());
        return false;
    }
    #ifndef NEWBASE_WII
    if (have_best_mode)
        SDL_SetWindowFullscreenMode(m_win, &best_mode);
    #endif

    return true;
}


render::window::~window()
{
    if(m_win)
    {
        log::info("[window] destroying");
        SDL_DestroyWindow(m_win);
    }
}


bool render::window::event(SDL_Event * evt)
{
    assert(m_win && "You must create() the window first!");

    if(evt->window.windowID != SDL_GetWindowID(m_win))
        return false; // still need to match event type to be sure!

    if(evt->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        m_pw = evt->window.data1;
        m_ph = evt->window.data2;
        log::info("[window] resized to %dx%d", m_pw, m_ph);
        return true;
    }
    else if(evt->type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED)
    {
        SDL_GetWindowSafeArea(m_win, &m_safe_area);
        log::info("[window] safe area: %dx%d@%d,%d", m_safe_area.w, m_safe_area.h, m_safe_area.x, m_safe_area.y);
        return true;
    }
    else
        return false;
}


bool render::window::show()
{
    assert(m_win && "You must create() the window first!");

    if(SDL_ShowWindow(m_win))
    {
        SDL_GetWindowSizeInPixels(m_win, &m_pw, &m_ph);
        m_ui_scale = SDL_GetWindowDisplayScale(m_win);
        SDL_GetWindowSafeArea(m_win, &m_safe_area);
        log::info("[window] opened, size %dx%d, ui_scale %f, safe area: %dx%d@%d,%d",
                  m_pw, m_ph, m_ui_scale,
                  m_safe_area.w, m_safe_area.h, m_safe_area.x, m_safe_area.y);
        return true;
    }

    return false;
}
