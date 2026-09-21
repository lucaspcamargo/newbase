#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_timer.h"
#include "entt/core/fwd.hpp"
#include "newbase/render/batcher2d.hpp"
#include "newbase/render/types.hpp"
#include <memory>
#include <newbase/ui/imgui_nb.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/nb_config.h>
#include <newbase/log.hpp>

#include <imgui.h>
#include <SDL3/SDL_time.h>
#include <entt/entt.hpp>


using namespace nb;

static const ImTextureRef FONT_REF = {ImTextureID{0}};
bool operator ==(const ImTextureRef &lhs, const ImTextureRef &rhs)
{
    return lhs._TexID == rhs._TexID;
}

struct nb::imgui_nb_p
{
    render::window &win;
    uint64_t time {0};

    render::batcher2d batcher {};
    std::shared_ptr<rtexture> font_tex;

    std::vector<render::vertex2d> cvt_buf;

    // int mouse_btns_down; -- This could help with handling dragging stuff past the window, bu we don't handle that
};

// Helpers
static void _imgui_nb_update_kmods(SDL_Keymod sdl_key_mods);
static ImGuiKey _imgui_nb_convert_key_evt(SDL_Keycode keycode, SDL_Scancode scancode);
static void _imgui_nb_update_monitors();


imgui_nb::imgui_nb() = default;
imgui_nb::~imgui_nb() = default;

void imgui_nb::init(render::window &win)
{
    assert(!_d);
    _d = std::make_unique<imgui_nb_p>(win);

    auto &io = ImGui::GetIO();
    assert(!io.BackendPlatformUserData && !io.BackendRendererUserData);
    io.BackendPlatformName = io.BackendRendererName = "newbase v" NEWBASE_VERSION;
    io.BackendPlatformUserData = io.BackendRendererUserData = this;
    io.BackendFlags = ImGuiBackendFlags_HasMouseCursors |
                        ImGuiBackendFlags_HasSetMousePos |
                        ImGuiBackendFlags_RendererHasTextures |
                        ImGuiBackendFlags_RendererHasVtxOffset;

    // create a texture resouce for the font atlas
    _d->font_tex = std::make_shared<rtexture>(entt::hashed_string("_imgui_nb_font"));
    io.Fonts->TexID = FONT_REF; // we will always use this special texture ref for the font atlas
    _rebuild_font_atlas(true);
}

void imgui_nb::teardown()
{
    assert(_d);
    _d.reset(nullptr);
}


void imgui_nb::new_frame(float delta)
{
    assert(_d);

    auto &io = ImGui::GetIO();
    auto w = _d->win.width();
    auto h = _d->win.height();
    auto s = _d->win.ui_scale();
    io.DisplaySize = ImVec2{ static_cast<float>(w/s), static_cast<float>(h/s) };
    io.DisplayFramebufferScale = ImVec2{s, s};

    // time control
    if(delta > 0.0)
    {
        io.DeltaTime = delta;
    }
    else
    {
        static Uint64 frequency = SDL_GetPerformanceFrequency();
        Uint64 current_time = SDL_GetPerformanceCounter();
        if (current_time <= _d->time)
            current_time = _d->time + 1;
        io.DeltaTime = _d->time > 0 ? (float)((double)(current_time - _d->time) / (double)frequency) : (float)(1.0f / 60.0f);
        _d->time = current_time;
    }

    _d->batcher.clear();
}


// update font atlas texture's upload data from the existing font data
void imgui_nb::_rebuild_font_atlas(bool force)
{
    auto &io = ImGui::GetIO();
    ImTextureRef curr_ref = io.Fonts->TexID;
    // Check if ImGui rebuilt the atlas internally or if it's missing a GPU handle
    if (!io.Fonts->IsBuilt() || force)
    {
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        // TODO use ALPHA8 here to save memory and bandwidth
        // May require changes to renderer code, shaders, etc?
        _d->font_tex->surf = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ARGB8888, pixels, width*4);
        _d->font_tex->width = width;
        _d->font_tex->height = height;
        _d->font_tex->uploaded = false;
        // The created surface is just a reference for ImGui's internal pixel buffer and can safely be destroyed,
        // as it won't touch its backing memory
        // This relies on the fact that the pointer remains constant as long as we don't call GetTex
    }
}

// Event handling
// This is a simplified rendition of ImGui_ImplSDL3_ProcessEvent
// Multi-viewport support has been striped out, as well as "display" stuff
bool imgui_nb::event(SDL_Event *event)
{
    ImGuiIO& io = ImGui::GetIO();

    switch (event->type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            ImVec2 mouse_pos((float)event->motion.x, (float)event->motion.y);
            io.AddMouseSourceEvent(event->motion.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
            io.AddMousePosEvent(mouse_pos.x, mouse_pos.y);
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            float wheel_x = -event->wheel.x;
            float wheel_y = event->wheel.y;
            io.AddMouseSourceEvent(event->wheel.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
            io.AddMouseWheelEvent(wheel_x, wheel_y);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            int mouse_button = -1;
            if (event->button.button == SDL_BUTTON_LEFT) { mouse_button = 0; }
            if (event->button.button == SDL_BUTTON_RIGHT) { mouse_button = 1; }
            if (event->button.button == SDL_BUTTON_MIDDLE) { mouse_button = 2; }
            if (event->button.button == SDL_BUTTON_X1) { mouse_button = 3; }
            if (event->button.button == SDL_BUTTON_X2) { mouse_button = 4; }
            if (mouse_button == -1)
                break;
            io.AddMouseSourceEvent(event->button.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
            io.AddMouseButtonEvent(mouse_button, (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN));
            //_d->mouse_btns_down = (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? (_d->mouse_btns_down | (1 << mouse_button)) : (_d->mouse_btns_down & ~(1 << mouse_button));
            return true;
        }
        case SDL_EVENT_TEXT_INPUT:
        {
            io.AddInputCharactersUTF8(event->text.text);
            return true;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            _imgui_nb_update_kmods((SDL_Keymod)event->key.mod);
            ImGuiKey key = _imgui_nb_convert_key_evt(event->key.key, event->key.scancode);
            io.AddKeyEvent(key, (event->type == SDL_EVENT_KEY_DOWN));
            io.SetKeyEventNativeData(key, (int)event->key.key, (int)event->key.scancode, (int)event->key.scancode);
            return true;
        }
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        {
            // mouse move will handle enter events
            return false;
        }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        {
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            return true;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        {
            io.AddFocusEvent(event->type == SDL_EVENT_WINDOW_FOCUS_GAINED);
            return true;
        }
        default:
            break;
    }
    return false;
}

void imgui_nb::render_flush()
{
    // ask ImGui to prepare all geometry for rendering
    ImGui::Render();

    auto scale = _d->win.ui_scale();
    auto &dd = *ImGui::GetDrawData();

    for(const auto dl: dd.CmdLists)
    {
        const ImDrawVert* vtx_buffer = dl->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer = dl->IdxBuffer.Data;

        // convert all vertex data in one shot
        _d->cvt_buf.clear();
        _d->cvt_buf.reserve(dl->VtxBuffer.size());
        for(const auto &vert:dl->VtxBuffer)
        {
            const auto fcol = ImGui::ColorConvertU32ToFloat4(vert.col);
            _d->cvt_buf.push_back(render::vertex2d{
                glm::vec2{vert.pos.x*scale, vert.pos.y*scale},  // scale UI to display scale, as ImGui emits logical coords
                glm::vec4{fcol.x, fcol.y, fcol.z, fcol.w},
                glm::vec2{vert.uv.x, vert.uv.y}
            });
            //log::info("VERT %f %f COL %f %f %f %f", vert.pos.x, vert.pos.y, fcol.x, fcol.y, fcol.z, fcol.w);
        }

        ImVec2 clip_off = dd.DisplayPos; // should be (0,0) as we don't use multi-viewports, but still
        ImVec2 clip_scale {scale, scale};

        // now convert commands using converted vertex data
        for(const auto &cmd: dl->CmdBuffer)
        {
            if(cmd.UserCallback)
                (cmd.UserCallback)(dl, &cmd);

            // we need to discover the maximum index value used by this command
            // to know the range of vertices to add to our drawing command
            // (our cmd vertex counts are bounded and accounted for)
            const ImDrawIdx* cmd_idx_base = idx_buffer + cmd.IdxOffset;
            ImDrawIdx max_idx = idx_buffer[0];
            for (unsigned int i = 1; i < cmd.ElemCount; ++i) {
                ImDrawIdx idx = cmd_idx_base[i];
                if (idx > max_idx) max_idx = idx;
            }
            uint32_t vtx_count = max_idx+1;



            // handle clipping
            ImVec2 clip_min = {
                (cmd.ClipRect.x + clip_off.x) * clip_scale.x,
                (cmd.ClipRect.y + clip_off.y) * clip_scale.y,
            };
            ImVec2 clip_max = {
                (cmd.ClipRect.z + clip_off.x) * clip_scale.x,
                (cmd.ClipRect.w + clip_off.y) * clip_scale.y,
            };
            ImVec2 clip_dims = {
                clip_max.x - clip_min.x,
                clip_max.y - clip_min.y
            };

            if(clip_dims.x <= 0.0f || clip_dims.y <= 0.0f)
                continue;  // skip empty or invalid clips

            render::clip_t clip {
                clip_min.x, clip_min.y,
                clip_dims.x, clip_dims.y
            };

            // TODO we need to figure out how to convert ImTextureRef <=> std::shared_ptr<rtexture>
            // this will also reduce or eliminate the need for the texture handling APIs in the renderer
            // ENTT resource IDs seem to be a good choice
            std::shared_ptr<rtexture> rtex = (cmd.TexRef == FONT_REF)? _d->font_tex : nullptr;

            _d->batcher.add_geom(_d->cvt_buf.data() + cmd.VtxOffset, vtx_count,
                                 cmd_idx_base, cmd.ElemCount,
                                 rtex, render::blendmode::BLEND, clip);
        }
    }

    // as the last rendering step, we will always update the atlaas texture's upload data if needed
    // the render system will take care of doing the upload
    _rebuild_font_atlas();
}

// getter for 2d batcher used to collet ImGui drawing data
render::batcher2d& imgui_nb::render_data()
{
    assert(_d);
    return _d->batcher;
}



//  Helpers impl

ImGuiKey _imgui_nb_convert_key_evt(SDL_Keycode keycode, SDL_Scancode scancode)
{
    // Keypad doesn't have individual key values in SDL3
    switch (scancode)
    {
        case SDL_SCANCODE_KP_0: return ImGuiKey_Keypad0;
        case SDL_SCANCODE_KP_1: return ImGuiKey_Keypad1;
        case SDL_SCANCODE_KP_2: return ImGuiKey_Keypad2;
        case SDL_SCANCODE_KP_3: return ImGuiKey_Keypad3;
        case SDL_SCANCODE_KP_4: return ImGuiKey_Keypad4;
        case SDL_SCANCODE_KP_5: return ImGuiKey_Keypad5;
        case SDL_SCANCODE_KP_6: return ImGuiKey_Keypad6;
        case SDL_SCANCODE_KP_7: return ImGuiKey_Keypad7;
        case SDL_SCANCODE_KP_8: return ImGuiKey_Keypad8;
        case SDL_SCANCODE_KP_9: return ImGuiKey_Keypad9;
        case SDL_SCANCODE_KP_PERIOD: return ImGuiKey_KeypadDecimal;
        case SDL_SCANCODE_KP_DIVIDE: return ImGuiKey_KeypadDivide;
        case SDL_SCANCODE_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case SDL_SCANCODE_KP_MINUS: return ImGuiKey_KeypadSubtract;
        case SDL_SCANCODE_KP_PLUS: return ImGuiKey_KeypadAdd;
        case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
        case SDL_SCANCODE_KP_EQUALS: return ImGuiKey_KeypadEqual;
        default: break;
    }
    switch (keycode)
    {
        case SDLK_TAB: return ImGuiKey_Tab;
        case SDLK_LEFT: return ImGuiKey_LeftArrow;
        case SDLK_RIGHT: return ImGuiKey_RightArrow;
        case SDLK_UP: return ImGuiKey_UpArrow;
        case SDLK_DOWN: return ImGuiKey_DownArrow;
        case SDLK_PAGEUP: return ImGuiKey_PageUp;
        case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
        case SDLK_HOME: return ImGuiKey_Home;
        case SDLK_END: return ImGuiKey_End;
        case SDLK_INSERT: return ImGuiKey_Insert;
        case SDLK_DELETE: return ImGuiKey_Delete;
        case SDLK_BACKSPACE: return ImGuiKey_Backspace;
        case SDLK_SPACE: return ImGuiKey_Space;
        case SDLK_RETURN: return ImGuiKey_Enter;
        case SDLK_ESCAPE: return ImGuiKey_Escape;
        //case SDLK_APOSTROPHE: return ImGuiKey_Apostrophe;
        case SDLK_COMMA: return ImGuiKey_Comma;
        //case SDLK_MINUS: return ImGuiKey_Minus;
        case SDLK_PERIOD: return ImGuiKey_Period;
        //case SDLK_SLASH: return ImGuiKey_Slash;
        case SDLK_SEMICOLON: return ImGuiKey_Semicolon;
        //case SDLK_EQUALS: return ImGuiKey_Equal;
        //case SDLK_LEFTBRACKET: return ImGuiKey_LeftBracket;
        //case SDLK_BACKSLASH: return ImGuiKey_Backslash;
        //case SDLK_RIGHTBRACKET: return ImGuiKey_RightBracket;
        //case SDLK_GRAVE: return ImGuiKey_GraveAccent;
        case SDLK_CAPSLOCK: return ImGuiKey_CapsLock;
        case SDLK_SCROLLLOCK: return ImGuiKey_ScrollLock;
        case SDLK_NUMLOCKCLEAR: return ImGuiKey_NumLock;
        case SDLK_PRINTSCREEN: return ImGuiKey_PrintScreen;
        case SDLK_PAUSE: return ImGuiKey_Pause;
        case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
        case SDLK_LSHIFT: return ImGuiKey_LeftShift;
        case SDLK_LALT: return ImGuiKey_LeftAlt;
        case SDLK_LGUI: return ImGuiKey_LeftSuper;
        case SDLK_RCTRL: return ImGuiKey_RightCtrl;
        case SDLK_RSHIFT: return ImGuiKey_RightShift;
        case SDLK_RALT: return ImGuiKey_RightAlt;
        case SDLK_RGUI: return ImGuiKey_RightSuper;
        case SDLK_APPLICATION: return ImGuiKey_Menu;
        case SDLK_0: return ImGuiKey_0;
        case SDLK_1: return ImGuiKey_1;
        case SDLK_2: return ImGuiKey_2;
        case SDLK_3: return ImGuiKey_3;
        case SDLK_4: return ImGuiKey_4;
        case SDLK_5: return ImGuiKey_5;
        case SDLK_6: return ImGuiKey_6;
        case SDLK_7: return ImGuiKey_7;
        case SDLK_8: return ImGuiKey_8;
        case SDLK_9: return ImGuiKey_9;
        case SDLK_A: return ImGuiKey_A;
        case SDLK_B: return ImGuiKey_B;
        case SDLK_C: return ImGuiKey_C;
        case SDLK_D: return ImGuiKey_D;
        case SDLK_E: return ImGuiKey_E;
        case SDLK_F: return ImGuiKey_F;
        case SDLK_G: return ImGuiKey_G;
        case SDLK_H: return ImGuiKey_H;
        case SDLK_I: return ImGuiKey_I;
        case SDLK_J: return ImGuiKey_J;
        case SDLK_K: return ImGuiKey_K;
        case SDLK_L: return ImGuiKey_L;
        case SDLK_M: return ImGuiKey_M;
        case SDLK_N: return ImGuiKey_N;
        case SDLK_O: return ImGuiKey_O;
        case SDLK_P: return ImGuiKey_P;
        case SDLK_Q: return ImGuiKey_Q;
        case SDLK_R: return ImGuiKey_R;
        case SDLK_S: return ImGuiKey_S;
        case SDLK_T: return ImGuiKey_T;
        case SDLK_U: return ImGuiKey_U;
        case SDLK_V: return ImGuiKey_V;
        case SDLK_W: return ImGuiKey_W;
        case SDLK_X: return ImGuiKey_X;
        case SDLK_Y: return ImGuiKey_Y;
        case SDLK_Z: return ImGuiKey_Z;
        case SDLK_F1: return ImGuiKey_F1;
        case SDLK_F2: return ImGuiKey_F2;
        case SDLK_F3: return ImGuiKey_F3;
        case SDLK_F4: return ImGuiKey_F4;
        case SDLK_F5: return ImGuiKey_F5;
        case SDLK_F6: return ImGuiKey_F6;
        case SDLK_F7: return ImGuiKey_F7;
        case SDLK_F8: return ImGuiKey_F8;
        case SDLK_F9: return ImGuiKey_F9;
        case SDLK_F10: return ImGuiKey_F10;
        case SDLK_F11: return ImGuiKey_F11;
        case SDLK_F12: return ImGuiKey_F12;
        case SDLK_F13: return ImGuiKey_F13;
        case SDLK_F14: return ImGuiKey_F14;
        case SDLK_F15: return ImGuiKey_F15;
        case SDLK_F16: return ImGuiKey_F16;
        case SDLK_F17: return ImGuiKey_F17;
        case SDLK_F18: return ImGuiKey_F18;
        case SDLK_F19: return ImGuiKey_F19;
        case SDLK_F20: return ImGuiKey_F20;
        case SDLK_F21: return ImGuiKey_F21;
        case SDLK_F22: return ImGuiKey_F22;
        case SDLK_F23: return ImGuiKey_F23;
        case SDLK_F24: return ImGuiKey_F24;
        case SDLK_AC_BACK: return ImGuiKey_AppBack;
        case SDLK_AC_FORWARD: return ImGuiKey_AppForward;
        default: break;
    }

    // Fallback to scancode
    switch (scancode)
    {
        case SDL_SCANCODE_GRAVE: return ImGuiKey_GraveAccent;
        case SDL_SCANCODE_MINUS: return ImGuiKey_Minus;
        case SDL_SCANCODE_EQUALS: return ImGuiKey_Equal;
        case SDL_SCANCODE_LEFTBRACKET: return ImGuiKey_LeftBracket;
        case SDL_SCANCODE_RIGHTBRACKET: return ImGuiKey_RightBracket;
        case SDL_SCANCODE_NONUSBACKSLASH: return ImGuiKey_Oem102;
        case SDL_SCANCODE_BACKSLASH: return ImGuiKey_Backslash;
        case SDL_SCANCODE_SEMICOLON: return ImGuiKey_Semicolon;
        case SDL_SCANCODE_APOSTROPHE: return ImGuiKey_Apostrophe;
        case SDL_SCANCODE_COMMA: return ImGuiKey_Comma;
        case SDL_SCANCODE_PERIOD: return ImGuiKey_Period;
        case SDL_SCANCODE_SLASH: return ImGuiKey_Slash;
        default: break;
    }
    return ImGuiKey_None;
}

void _imgui_nb_update_kmods(SDL_Keymod sdl_key_mods)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, (sdl_key_mods & SDL_KMOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (sdl_key_mods & SDL_KMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (sdl_key_mods & SDL_KMOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (sdl_key_mods & SDL_KMOD_GUI) != 0);
}
