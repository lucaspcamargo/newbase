#include <newbase/render/window.hpp>
#include <newbase/log.hpp>

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_version.h"
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

    SDL_PropertiesID props = SDL_GetWindowProperties(m_win);
    SDL_SetPointerProperty(props, "nb::render::window", this);

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
    else if(evt->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
    {
        m_ui_scale = SDL_GetWindowDisplayScale(m_win);
        m_event_scale = SDL_GetWindowPixelDensity(m_win);
        log::info("[window] scale chnged, ui=%f, event=%f", m_ui_scale, m_event_scale);
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
        m_event_scale = SDL_GetWindowPixelDensity(m_win);
        SDL_GetWindowSafeArea(m_win, &m_safe_area);
        log::info("[window] opened, size %dx%d, ui_scale %f, safe area: %dx%d@%d,%d",
                  m_pw, m_ph, m_ui_scale,
                  m_safe_area.w, m_safe_area.h, m_safe_area.x, m_safe_area.y);
        return true;
    }

    return false;
}

void render::window::center()
{
    assert(m_win && "You must create() the window first!");

#ifndef NEWBASE_WII
    SDL_SetWindowPosition(m_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
#endif

}


static inline void _transform_event(SDL_Event &evt, float scale, SDL_WindowID wid)
{
    switch (evt.type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            if(evt.motion.windowID == wid)
            {
                evt.motion.x *= scale;
                evt.motion.y *= scale;
                evt.motion.xrel *= scale;
                evt.motion.yrel *= scale;
            }
        }
        break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            [[fallthrough]];
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if(evt.button.windowID == wid)
            {
                evt.button.x *= scale;
                evt.button.y *= scale;
            }
        }
        break;

        case SDL_EVENT_DROP_FILE:
            [[fallthrough]];
        case SDL_EVENT_DROP_TEXT:
            [[fallthrough]];
        case SDL_EVENT_DROP_COMPLETE:
        {
            // docs say SDL_EVENT_DROP_BEGIN doesn't have a position
            if(evt.drop.windowID == wid)
            {
                evt.drop.x *= scale;
                evt.drop.y *= scale;
            }
        };

        case SDL_EVENT_PEN_MOTION:
        {
            if(evt.pmotion.windowID == wid)
            {
                evt.pmotion.x *= scale;
                evt.pmotion.y *= scale;
            }
        }
        break;

        case SDL_EVENT_PEN_BUTTON_DOWN:
            [[fallthrough]];
        case SDL_EVENT_PEN_BUTTON_UP:
        {
            if(evt.pbutton.windowID == wid)
            {
                evt.pbutton.x *= scale;
                evt.pbutton.y *= scale;
            }
        }
        break;

        case SDL_EVENT_PEN_DOWN:
            [[fallthrough]];
        case SDL_EVENT_PEN_UP:
        {
            if(evt.ptouch.windowID == wid)
            {
                evt.ptouch.x *= scale;
                evt.ptouch.y *= scale;
            }
        }
        break;


        case SDL_EVENT_PEN_AXIS:
        {
            if(evt.paxis.windowID == wid)
            {
                evt.paxis.x *= scale;
                evt.paxis.y *= scale;
            }
        }
        break;


#if SDL_VERSION_ATLEAST(3, 6, 0)
        case SDL_EVENT_PINCH_BEGIN:
            [[fallthrough]];
        case SDL_EVENT_PINCH_UPDATE:
        {
            if(evt.pinch.windowID == wid)
            {
                if(evt.pinch.span_x != -1.f)
                    evt.pinch.span_x *= scale;
                if(evt.pinch.span_y != -1.f)
                    evt.pinch.span_y *= scale;
                if(evt.pinch.focus_x != -1.f)
                    evt.pinch.focus_x *= scale;
                if(evt.pinch.focus_y != -1.f)
                    evt.pinch.focus_y *= scale;
            }
        };
#endif

        case SDL_EVENT_FINGER_MOTION:
            [[fallthrough]];
        case SDL_EVENT_FINGER_UP:
            [[fallthrough]];
        case SDL_EVENT_FINGER_DOWN:
        {
            // touch events are normalized in window dimensions, so are the same
            // in ui and pixel coordinates
            // do nothing :)
        }
        break;
    }
}


SDL_Event render::window::event_to_pixel_coordinates(const SDL_Event* evt) const
{
    SDL_Event ret = *evt;
    const float scale = m_event_scale;
    _transform_event(ret, scale, m_wid);
    return ret;
}


SDL_Event render::window::event_to_ui_coordinates(const SDL_Event* evt) const
{
    SDL_Event ret = *evt;
    const float scale = m_event_scale / m_ui_scale;
    _transform_event(ret, scale, m_wid);
    return ret;
}


render::window* render::window::from_sdl_window(SDL_Window *w)
{
    if(!w)
        return nullptr; // duh

    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    return static_cast<window*>(SDL_GetPointerProperty(props, "nb::render::window", nullptr));
}
