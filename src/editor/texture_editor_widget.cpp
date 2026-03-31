#include <newbase/editor/texture_editor_widget.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/services/renderer_service.hpp>
#include <entt/locator/locator.hpp>
#include <imgui.h>
#include "IconsForkAwesome.h"
#include <SDL3/SDL_pixels.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <queue>
#include <string>
#include <utility>

namespace nb {

// ── Pastel palette ────────────────────────────────────────────────────────────
// 6 columns × 5 rows: pinks → oranges/yellows → greens → blues/purples → neutrals
// Each row goes light → saturated → dark.

// Two horizontal strips: warm colours on top, cool + neutrals on bottom.
static const ImVec4 kPalette[30] = {
    // Row 1 – Warm: pinks × 5, oranges × 3, yellows × 3, greens × 3, teal × 1
    {1.00f, 0.80f, 0.85f, 1}, {1.00f, 0.60f, 0.70f, 1}, {0.98f, 0.40f, 0.56f, 1},
    {0.85f, 0.22f, 0.40f, 1}, {0.42f, 0.04f, 0.16f, 1},
    {1.00f, 0.88f, 0.76f, 1}, {1.00f, 0.73f, 0.47f, 1}, {1.00f, 0.57f, 0.22f, 1},
    {1.00f, 1.00f, 0.80f, 1}, {0.99f, 0.98f, 0.55f, 1}, {0.95f, 0.85f, 0.20f, 1},
    {0.75f, 0.95f, 0.78f, 1}, {0.47f, 0.87f, 0.53f, 1}, {0.18f, 0.65f, 0.28f, 1},
    {0.72f, 0.95f, 0.88f, 1},
    // Row 2 – Cool + Neutrals: teals × 2, blues × 3, purples × 3, grays × 6, black
    {0.38f, 0.82f, 0.70f, 1}, {0.10f, 0.55f, 0.45f, 1},
    {0.72f, 0.90f, 0.98f, 1}, {0.42f, 0.72f, 0.95f, 1}, {0.14f, 0.44f, 0.80f, 1},
    {0.82f, 0.75f, 0.95f, 1}, {0.60f, 0.45f, 0.82f, 1}, {0.32f, 0.18f, 0.60f, 1},
    {1.00f, 1.00f, 1.00f, 1}, {0.82f, 0.82f, 0.82f, 1}, {0.62f, 0.62f, 0.62f, 1},
    {0.42f, 0.42f, 0.42f, 1}, {0.20f, 0.20f, 0.20f, 1}, {0.00f, 0.00f, 0.00f, 1},
    {0.00f, 0.00f, 0.00f, 0}, // transparent slot
};
static constexpr int kPaletteCols = 15;
static constexpr int kPaletteRows = 2;

// ── Lifecycle ────────────────────────────────────────────────────────────────

texture_editor_widget::~texture_editor_widget()
{
    if (_tex)
    {
        auto* rs = entt::locator<renderer_service*>::has_value()
                   ? entt::locator<renderer_service*>::value() : nullptr;
        if (rs) rs->destroy_texture(_tex);
    }
    if (_canvas) SDL_DestroySurface(_canvas);
}

texture_editor_widget::texture_editor_widget(texture_editor_widget&& o) noexcept
    : _canvas(o._canvas), _tex(o._tex),
      _tool(o._tool), _primary(o._primary), _secondary(o._secondary),
      _brush_size(o._brush_size), _zoom(o._zoom),
      _dirty(o._dirty), _last_pos(o._last_pos),
      _line_start(o._line_start), _line_end(o._line_end), _line_primary(o._line_primary)
{
    o._canvas = nullptr;
    o._tex    = nullptr;
}

texture_editor_widget& texture_editor_widget::operator=(texture_editor_widget&& o) noexcept
{
    if (this != &o)
    {
        this->~texture_editor_widget();
        new (this) texture_editor_widget(std::move(o));
    }
    return *this;
}

void texture_editor_widget::apply(resource* res)
{
    if (!_canvas || !res) return;
    auto* rt = static_cast<rtexture*>(res);
    if (rt->surf) SDL_DestroySurface(rt->surf);
    rt->surf = SDL_DuplicateSurface(_canvas);

    // hear me out
    // it's arguable we may not always want to do this, but...
    // let me give it a go
    // at some point rtexture needs to become much more complex and opinionated than this... or not :)
    // As I understand, the GUI can only use it's own textures for rendering (created via the renderer service), no imgui renderer troubles
    // decreed that the renderer must know how to cope with this for now
    // It can be our little unspoken bounding agreement promise interface thinguie...
    // ...or basically, the interface of the component
    SDL_DestroyTexture(rt->tex);
    rt->tex = nullptr;
    rt->uploaded = false; // betty_boop_oops.gif
}

void texture_editor_widget::open(SDL_Surface* source)
{
    if (_tex)
    {
        auto* rs = entt::locator<renderer_service*>::has_value()
                   ? entt::locator<renderer_service*>::value() : nullptr;
        if (rs) rs->destroy_texture(_tex);
        _tex = nullptr;
    }
    if (_canvas) { SDL_DestroySurface(_canvas); _canvas = nullptr; }

    if (!source) return;

    _canvas = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    if (!_canvas) return;

    auto* rs = entt::locator<renderer_service*>::has_value()
               ? entt::locator<renderer_service*>::value() : nullptr;
    if (!rs) return;

    _tex = rs->create_texture(_canvas->w, _canvas->h);
    rs->update_texture(_tex, _canvas->pixels, _canvas->pitch);

    // Start zoomed so the texture fills roughly 256px
    float max_dim = (float)std::max(_canvas->w, _canvas->h);
    _zoom      = std::clamp(256.0f / max_dim, 0.25f, 8.0f);
    _brush_size = 4;
    _tool      = tool_t::pencil;
    _dirty     = false;
    _last_pos  = {-1.0f, -1.0f};
}

// ── Pixel helpers ────────────────────────────────────────────────────────────

Uint32 texture_editor_widget::_get_pixel(int x, int y) const
{
    const Uint8* p = (const Uint8*)_canvas->pixels + y * _canvas->pitch + x * 4;
    Uint32 px; memcpy(&px, p, 4);
    return px;
}

void texture_editor_widget::_set_pixel(int x, int y, Uint32 pixel)
{
    Uint8* p = (Uint8*)_canvas->pixels + y * _canvas->pitch + x * 4;
    memcpy(p, &pixel, 4);
}

Uint32 texture_editor_widget::_to_pixel(ImVec4 c) const
{
    Uint8 r = (Uint8)(c.x * 255.0f + 0.5f);
    Uint8 g = (Uint8)(c.y * 255.0f + 0.5f);
    Uint8 b = (Uint8)(c.z * 255.0f + 0.5f);
    Uint8 a = (Uint8)(c.w * 255.0f + 0.5f);
    return SDL_MapRGBA(SDL_GetPixelFormatDetails(_canvas->format), nullptr, r, g, b, a);
}

ImVec4 texture_editor_widget::_from_pixel(Uint32 pixel) const
{
    Uint8 r, g, b, a;
    SDL_GetRGBA(pixel, SDL_GetPixelFormatDetails(_canvas->format), nullptr, &r, &g, &b, &a);
    return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

// ── Painting ─────────────────────────────────────────────────────────────────

void texture_editor_widget::_paint_dot(int cx, int cy, ImVec4 color)
{
    Uint32 pixel = _to_pixel(color);
    int r = _brush_size / 2;
    for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
    {
        if (r > 0 && dx * dx + dy * dy > r * r) continue;
        int px = cx + dx, py = cy + dy;
        if (px >= 0 && py >= 0 && px < _canvas->w && py < _canvas->h)
            _set_pixel(px, py, pixel);
    }
}

void texture_editor_widget::_paint_stroke(int x0, int y0, int x1, int y1, ImVec4 color)
{
    int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    for (int i = 0; i <= steps; i++)
    {
        float t = steps > 0 ? (float)i / steps : 0.0f;
        _paint_dot((int)(x0 + t * (x1 - x0) + 0.5f),
                   (int)(y0 + t * (y1 - y0) + 0.5f), color);
    }
}

void texture_editor_widget::_flood_fill(int x, int y, ImVec4 replacement)
{
    if (!_canvas || x < 0 || y < 0 || x >= _canvas->w || y >= _canvas->h) return;
    Uint32 target = _get_pixel(x, y);
    Uint32 fill   = _to_pixel(replacement);
    if (target == fill) return;

    std::queue<std::pair<int,int>> q;
    q.push({x, y});
    while (!q.empty())
    {
        auto [cx, cy] = q.front(); q.pop();
        if (cx < 0 || cy < 0 || cx >= _canvas->w || cy >= _canvas->h) continue;
        if (_get_pixel(cx, cy) != target) continue;
        _set_pixel(cx, cy, fill);
        q.push({cx + 1, cy}); q.push({cx - 1, cy});
        q.push({cx, cy + 1}); q.push({cx, cy - 1});
    }
    _dirty = true;
}

void texture_editor_widget::_push_texture()
{
    if (!_canvas || !_tex) return;
    auto* rs = entt::locator<renderer_service*>::has_value()
               ? entt::locator<renderer_service*>::value() : nullptr;
    if (rs) rs->update_texture(_tex, _canvas->pixels, _canvas->pitch);
}

// ── Toolbar ───────────────────────────────────────────────────────────────────

void texture_editor_widget::_toolbar(ImVec2 canvas_avail)
{
    auto tool_btn = [&](const char* icon, tool_t t, const char* tip)
    {
        bool active = (_tool == t);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(icon)) _tool = t;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    };

    tool_btn(ICON_FK_PENCIL,     tool_t::pencil,     "Pencil  [LMB=primary  RMB=secondary]");
    tool_btn(ICON_FK_MINUS,      tool_t::line,       "Line  [click+drag, LMB=primary  RMB=secondary]");
    tool_btn(ICON_FK_ERASER,     tool_t::eraser,     "Eraser  [paints transparent]");
    tool_btn(ICON_FK_TINT,       tool_t::fill,       "Fill bucket  [LMB=primary  RMB=secondary]");
    tool_btn(ICON_FK_EYEDROPPER, tool_t::eyedropper, "Eyedropper  [pick color, returns to pencil]");

    ImGui::TextDisabled("|"); ImGui::SameLine();

    static const int kSizes[] = {1, 2, 4, 8, 16};
    for (int i = 0; i < 5; i++)
    {
        bool active = (_brush_size == kSizes[i]);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(std::to_string(kSizes[i]).c_str())) _brush_size = kSizes[i];
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Brush size %d", kSizes[i]);
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    ImGui::TextDisabled("|"); ImGui::SameLine();

    // Overlapping primary/secondary color swatches
    // secondary drawn behind+offset, primary on top via window draw list
    const float sw = 20.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##swatches", {sw + 7.0f, sw + 6.0f});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // secondary (offset right+down)
    ImVec2 s0 = {origin.x + 7, origin.y + 6};
    dl->AddRectFilled(s0, {s0.x + sw, s0.y + sw}, ImGui::ColorConvertFloat4ToU32(_secondary));
    dl->AddRect      (s0, {s0.x + sw, s0.y + sw}, IM_COL32(60, 60, 60, 255));
    // primary (front)
    dl->AddRectFilled(origin, {origin.x + sw, origin.y + sw}, ImGui::ColorConvertFloat4ToU32(_primary));
    dl->AddRect      (origin, {origin.x + sw, origin.y + sw}, IM_COL32(60, 60, 60, 255));

    if (ImGui::IsItemClicked(0))      ImGui::OpenPopup("##pri_picker");
    if (ImGui::IsItemClicked(1))      ImGui::OpenPopup("##sec_picker");
    if (ImGui::IsItemClicked(2))      std::swap(_primary, _secondary);
    if (ImGui::IsItemHovered())       ImGui::SetTooltip("L: primary   R: secondary   M: swap");

    if (ImGui::BeginPopup("##pri_picker"))
    { ImGui::ColorPicker4("##p", &_primary.x,   ImGuiColorEditFlags_AlphaBar); ImGui::EndPopup(); }
    if (ImGui::BeginPopup("##sec_picker"))
    { ImGui::ColorPicker4("##s", &_secondary.x, ImGuiColorEditFlags_AlphaBar); ImGui::EndPopup(); }

    ImGui::SameLine();
    ImGui::TextDisabled("|"); ImGui::SameLine();
    ImGui::TextDisabled("%dx%d  %.0f%%", _canvas->w, _canvas->h, _zoom * 100.0f);

    // Zoom-to-fit button
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FK_ARROWS_ALT))
        _zoom = std::clamp(std::min(canvas_avail.x / _canvas->w, canvas_avail.y / _canvas->h), 0.125f, 32.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom to fit");
}

// ── Canvas ────────────────────────────────────────────────────────────────────

void texture_editor_widget::_draw_canvas(ImVec2 size)
{
    ImGui::BeginChild("##cvs", size, ImGuiChildFlags_Border,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 display = {_canvas->w * _zoom, _canvas->h * _zoom};
    ImVec2 avail   = ImGui::GetContentRegionAvail();

    // Center the canvas when it fits inside the child window
    float ox = (display.x < avail.x) ? (avail.x - display.x) * 0.5f : 0.0f;
    float oy = (display.y < avail.y) ? (avail.y - display.y) * 0.5f : 0.0f;
    ImGui::SetCursorPos({ox, oy});

    ImVec2 canvas_screen = ImGui::GetCursorScreenPos();

    // ── Checkerboard (transparency indicator) ───────────────────────────────
    const float cs = std::max(2.0f * _zoom, 8.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int tiles_x = (int)ceilf(display.x / cs);
    const int tiles_y = (int)ceilf(display.y / cs);
    for (int iy = 0; iy < tiles_y; iy++)
    for (int ix = 0; ix < tiles_x; ix++)
    {
        float x0 = ix * cs, y0 = iy * cs;
        dl->AddRectFilled(
            {canvas_screen.x + x0,                          canvas_screen.y + y0},
            {canvas_screen.x + std::min(x0 + cs, display.x),
             canvas_screen.y + std::min(y0 + cs, display.y)},
            (iy + ix) % 2 ? IM_COL32(168, 168, 168, 255) : IM_COL32(218, 218, 218, 255));
    }

    // ── Texture image ───────────────────────────────────────────────────────
    ImGui::Image((ImTextureID)_tex, display);

    // ── Invisible button on top for mouse interaction ───────────────────────
    ImGui::SetCursorScreenPos(canvas_screen);
    ImGui::InvisibleButton("##paint", display,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();

    // Reset stroke when no button held
    bool lmb = ImGui::IsMouseDown(0);
    bool rmb = ImGui::IsMouseDown(1);
    if (!lmb && !rmb) _last_pos = {-1.0f, -1.0f};

    // Commit line on mouse release (checked globally, not just while hovered)
    if (_tool == tool_t::line && _line_start.x >= 0 && !lmb && !rmb && _line_end.x >= 0)
    {
        _paint_stroke((int)_line_start.x, (int)_line_start.y,
                      (int)_line_end.x,   (int)_line_end.y,
                      _line_primary ? _primary : _secondary);
        _dirty = true;
        _line_start = _line_end = {-1.0f, -1.0f};
    }

    if (hov || act)
    {
        ImGuiIO& io = ImGui::GetIO();
        float cx = (io.MousePos.x - canvas_screen.x) / _zoom;
        float cy = (io.MousePos.y - canvas_screen.y) / _zoom;
        int px = (int)cx, py = (int)cy;
        bool in_bounds = px >= 0 && py >= 0 && px < _canvas->w && py < _canvas->h;

        // Ctrl+Wheel → zoom (anchored roughly to cursor)
        if (hov && io.MouseWheel != 0.0f && io.KeyCtrl)
            _zoom = std::clamp(_zoom * powf(1.25f, io.MouseWheel), 0.125f, 32.0f);

        // Middle-drag → pan
        if (ImGui::IsMouseDragging(2))
        {
            ImVec2 delta = ImGui::GetMouseDragDelta(2, 0.0f);
            ImGui::ResetMouseDragDelta(2);
            ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
        }

        // Pixel tooltip
        if (hov && in_bounds)
        {
            ImVec4 hc = _from_pixel(_get_pixel(px, py));
            ImGui::SetTooltip("(%d, %d)   r%d g%d b%d a%d",
                px, py,
                (int)(hc.x * 255), (int)(hc.y * 255),
                (int)(hc.z * 255), (int)(hc.w * 255));
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); // for finger painting
        }

        if (in_bounds)
        {
            switch (_tool)
            {
            case tool_t::pencil:
            case tool_t::eraser:
                if ((lmb || rmb) && act)
                {
                    ImVec4 col = lmb ? _primary : _secondary;
                    if (_tool == tool_t::eraser) col = {0, 0, 0, 0};
                    if (_last_pos.x < 0)
                        _paint_dot(px, py, col);
                    else
                        _paint_stroke((int)_last_pos.x, (int)_last_pos.y, px, py, col);
                    _last_pos = {(float)px, (float)py};
                    _dirty = true;
                }
                break;

            case tool_t::fill:
                if (ImGui::IsMouseClicked(0)) _flood_fill(px, py, _primary);
                if (ImGui::IsMouseClicked(1)) _flood_fill(px, py, _secondary);
                break;

            case tool_t::eyedropper:
                if (ImGui::IsMouseClicked(0)) { _primary   = _from_pixel(_get_pixel(px, py)); _tool = tool_t::pencil; }
                if (ImGui::IsMouseClicked(1)) { _secondary = _from_pixel(_get_pixel(px, py)); _tool = tool_t::pencil; }
                break;

            case tool_t::line:
                if (ImGui::IsMouseClicked(0)) { _line_start = {(float)px, (float)py}; _line_end = _line_start; _line_primary = true; }
                if (ImGui::IsMouseClicked(1)) { _line_start = {(float)px, (float)py}; _line_end = _line_start; _line_primary = false; }
                break;
            }
        }

        // Line preview: update endpoint and draw overlay (works even outside canvas bounds)
        if (_tool == tool_t::line && _line_start.x >= 0 && (lmb || rmb))
        {
            float ex = std::clamp(cx, 0.0f, (float)(_canvas->w - 1));
            float ey = std::clamp(cy, 0.0f, (float)(_canvas->h - 1));
            _line_end = {ex, ey};

            ImVec4 prevcol = _line_primary ? _primary : _secondary;
            float  lw      = std::max(1.0f, (float)_brush_size * _zoom * 0.5f);
            ImVec2 p0 = {canvas_screen.x + (_line_start.x + 0.5f) * _zoom,
                         canvas_screen.y + (_line_start.y + 0.5f) * _zoom};
            ImVec2 p1 = {canvas_screen.x + (ex + 0.5f) * _zoom,
                         canvas_screen.y + (ey + 0.5f) * _zoom};
            dl->AddLine(p0, p1, ImGui::ColorConvertFloat4ToU32(prevcol), lw);
        }
    }

    if (_dirty) { _push_texture(); _dirty = false; }

    ImGui::EndChild();
}

// ── Palette ───────────────────────────────────────────────────────────────────

void texture_editor_widget::_palette(float swatch_size)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoBorder
                              | ImGuiColorEditFlags_NoTooltip
                              | ImGuiColorEditFlags_AlphaPreview;

    for (int i = 0; i < kPaletteRows * kPaletteCols; i++)
    {
        if (i % kPaletteCols != 0) ImGui::SameLine(0, spacing * 0.5f);
        ImGui::PushID(i);
        // ColorButton returns true on LMB click
        if (ImGui::ColorButton("##c", kPalette[i], flags, {swatch_size, swatch_size}))
            _primary = kPalette[i];
        if (ImGui::IsItemClicked(1))
            _secondary = kPalette[i];
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("L: primary   R: secondary\n#%02X%02X%02X",
                (int)(kPalette[i].x * 255), (int)(kPalette[i].y * 255), (int)(kPalette[i].z * 255));
        ImGui::PopID();
    }
}

// ── Main draw ─────────────────────────────────────────────────────────────────

void texture_editor_widget::draw()
{
    if (!_canvas || !_tex) { ImGui::TextDisabled("(no texture)"); return; }

    const float swatch_size = 20.0f;
    const float sp          = ImGui::GetStyle().ItemSpacing.y;
    const float sep_h       = sp + 1.0f;
    const float toolbar_h   = ImGui::GetFrameHeight() + sp;
    const float palette_h   = swatch_size * kPaletteRows + sp * (kPaletteRows - 1) + sep_h + 2.0f;

    ImVec2 total      = ImGui::GetContentRegionAvail();
    ImVec2 canvas_avail = {total.x, total.y - toolbar_h - sep_h - palette_h};

    _toolbar(canvas_avail);
    ImGui::Separator();

    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.y -= palette_h;

    _draw_canvas(canvas_size);
    ImGui::Separator();
    _palette(swatch_size);
}

} // namespace nb
