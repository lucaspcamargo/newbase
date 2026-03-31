#pragma once

#include <SDL3/SDL_surface.h>
#include <imgui.h>

namespace nb {

class resource;

// A self-contained MS-Paint-style texture editor widget.
// Call open() once with the source surface (it makes its own copy),
// then call draw() each frame inside an already-begun ImGui window.
class texture_editor_widget
{
public:
    texture_editor_widget() = default;
    ~texture_editor_widget();

    texture_editor_widget(const texture_editor_widget&) = delete;
    texture_editor_widget& operator=(const texture_editor_widget&) = delete;
    texture_editor_widget(texture_editor_widget&&) noexcept;
    texture_editor_widget& operator=(texture_editor_widget&&) noexcept;

    void open(SDL_Surface* source);
    void draw();

    // Sync the edited canvas back into the resource's surface.
    // Must be called before rman().save_resource() to ensure the resource has the latest pixels.
    void apply(resource* res);

private:
    enum class tool_t { pencil, fill, eraser, eyedropper, line };

    void _toolbar(ImVec2 canvas_avail);
    void _draw_canvas(ImVec2 size);
    void _palette(float swatch_size);

    void _paint_dot(int cx, int cy, ImVec4 color);
    void _paint_stroke(int x0, int y0, int x1, int y1, ImVec4 color);
    void _flood_fill(int x, int y, ImVec4 replacement);
    void _push_texture();

    Uint32 _get_pixel(int x, int y) const;
    void   _set_pixel(int x, int y, Uint32 pixel);
    Uint32 _to_pixel(ImVec4 c) const;
    ImVec4 _from_pixel(Uint32 pixel) const;

    SDL_Surface* _canvas  {nullptr};
    void*        _tex     {nullptr};

    tool_t _tool          {tool_t::pencil};
    ImVec4 _primary       {0.18f, 0.18f, 0.18f, 1.0f};
    ImVec4 _secondary     {1.00f, 1.00f, 1.00f, 1.0f};
    int    _brush_size    {4};
    float  _zoom          {1.0f};
    bool   _dirty         {false};
    ImVec2 _last_pos      {-1.0f, -1.0f};

    // line tool state
    ImVec2 _line_start    {-1.0f, -1.0f}; // canvas coords when LMB/RMB first pressed
    ImVec2 _line_end      {-1.0f, -1.0f}; // last known canvas endpoint (clamped)
    bool   _line_primary  {true};          // which color the line uses
};

} // namespace nb
