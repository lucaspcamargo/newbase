#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include <newbase/ui/layout.hpp>
#include <cstdint>

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cmath>

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

namespace {

enum class LayoutType { Horizontal, Vertical };

struct LayoutItem
{
    enum Kind { Item, Spring } kind = Item;

    // Bounding rect measured during this frame.
    ImRect measured = {};

    // Vertex range in the window draw list for post-hoc translation.
    unsigned int vtx_begin = 0;
    unsigned int vtx_end   = 0;

    // Cross-axis alignment [0..1], -1 = inherit from layout.
    float align        = -1.f;
    float align_offset = 0.f;

    // Spring fields (kind == Spring only).
    float spring_weight  = 1.f;
    float spring_spacing = 0.f;
    float spring_size    = 0.f; // carried over from previous frame
};

struct Layout
{
    ImGuiID    id   = 0;
    LayoutType type = LayoutType::Horizontal;

    ImVec2 requested_size = {};  // user-supplied (0 = auto)
    ImVec2 current_size   = {};  // effective size this frame
    ImVec2 minimum_size   = {};  // content size with springs collapsed
    ImVec2 measured_size  = {};  // final size including springs

    ImVec2 start_pos          = {};
    ImVec2 start_cursor_max   = {};
    float  indent             = 0.f;
    float  align              = -1.f; // -1 = use g.Style.LayoutAlign (0.5)

    bool live = false;

    int current_item_idx = 0;
    std::vector<LayoutItem> items;

    // Child-layout tree (rebuilt each frame).
    Layout* parent       = nullptr;
    Layout* first_child  = nullptr;
    Layout* next_sibling = nullptr;
    int     parent_item_idx = 0;

    // Draw-list splitter for clip-rect correction.
    ImDrawListSplitter splitter;
};

// ---------------------------------------------------------------------------
// Global state  (all accesses happen on the main thread, single imgui context)
// ---------------------------------------------------------------------------

static std::unordered_map<ImGuiID, Layout*> g_layouts;  // persistent across frames
static std::vector<Layout*>                 g_stack;     // currently open layouts
static Layout*                              g_current  = nullptr;
static LayoutItem*                          g_cur_item = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static Layout*     find_or_create(ImGuiID id, LayoutType type);
static void        begin_layout(ImGuiID id, LayoutType type, ImVec2 size, float align);
static void        end_layout(LayoutType type);
static void        push_layout(Layout* l);
static void        pop_layout();
static void        begin_layout_item(Layout& l);
static void        end_layout_item(Layout& l);
static void        add_spring(Layout& l, float weight, float spacing);
static void        translate_item(LayoutItem& item, ImVec2 offset);
static float       calc_alignment_offset(const Layout& l, const LayoutItem& item);
static void        balance_springs(Layout& l);
static void        balance_items_alignment(Layout& l);
static void        balance_children(Layout& l);
static ImVec2      calc_layout_size(const Layout& l, bool collapse_springs);
static void        signed_indent(float v);

// ---------------------------------------------------------------------------
// Layout persistence helpers
// ---------------------------------------------------------------------------

static Layout* find_or_create(ImGuiID id, LayoutType type)
{
    auto it = g_layouts.find(id);
    if (it != g_layouts.end())
    {
        Layout* l = it->second;
        if (l->type != type)
        {
            l->type         = type;
            l->minimum_size = {};
            l->items.clear();
        }
        return l;
    }
    Layout* l = new Layout();
    l->id   = id;
    l->type = type;
    g_layouts[id] = l;
    return l;
}

// ---------------------------------------------------------------------------
// Stack management
// ---------------------------------------------------------------------------

static void push_layout(Layout* l)
{
    if (l)
    {
        l->parent = g_current;
        if (l->parent)
        {
            l->parent_item_idx = l->parent->current_item_idx;
            l->next_sibling    = l->parent->first_child;
            l->parent->first_child = l;
        }
        else
        {
            l->next_sibling = nullptr;
        }
        l->first_child = nullptr;
    }
    g_stack.push_back(l);
    g_current  = l;
    g_cur_item = nullptr;
}

static void pop_layout()
{
    assert(!g_stack.empty());
    g_stack.pop_back();
    if (!g_stack.empty())
    {
        g_current  = g_stack.back();
        g_cur_item = g_current ? &g_current->items[g_current->current_item_idx] : nullptr;
    }
    else
    {
        g_current  = nullptr;
        g_cur_item = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Vertex translation
// ---------------------------------------------------------------------------

static void translate_item(LayoutItem& item, ImVec2 offset)
{
    if ((offset.x == 0.f && offset.y == 0.f) || item.vtx_begin == item.vtx_end)
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImDrawVert* begin = dl->VtxBuffer.Data + item.vtx_begin;
    ImDrawVert* end   = dl->VtxBuffer.Data + item.vtx_end;
    for (ImDrawVert* v = begin; v < end; ++v)
    {
        v->pos.x += offset.x;
        v->pos.y += offset.y;
    }
}

// ---------------------------------------------------------------------------
// Alignment helpers
// ---------------------------------------------------------------------------

static float calc_alignment_offset(const Layout& l, const LayoutItem& item)
{
    if (item.align <= 0.f)
        return 0.f;

    ImVec2 sz = item.measured.GetSize();
    float layout_extent = (l.type == LayoutType::Horizontal) ? l.current_size.y : l.current_size.x;
    float item_extent   = (l.type == LayoutType::Horizontal) ? sz.y : sz.x;

    if (item_extent <= 0.f)
        return 0.f;

    return std::floor(item.align * (layout_extent - item_extent));
}

static void signed_indent(float v)
{
    if (v > 0.f)       ImGui::Indent(v);
    else if (v < 0.f)  ImGui::Unindent(-v);
}

// ---------------------------------------------------------------------------
// Item slot management
// ---------------------------------------------------------------------------

static LayoutItem& generate_item(Layout& l, LayoutItem::Kind kind)
{
    assert(l.current_item_idx <= (int)l.items.size());
    if (l.current_item_idx < (int)l.items.size())
    {
        LayoutItem& item = l.items[l.current_item_idx];
        if (item.kind != kind)
            item = LayoutItem{};
        item.kind = kind;
        g_cur_item = &item;
        return item;
    }
    l.items.push_back(LayoutItem{});
    l.items.back().kind = kind;
    g_cur_item = &l.items.back();
    return l.items.back();
}

static void begin_layout_item(Layout& l)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow*  w = g.CurrentWindow;

    LayoutItem& item = generate_item(l, LayoutItem::Item);

    item.align = l.align;
    if (item.align < 0.f)
        item.align = ImClamp(g.Style.DisabledAlpha >= 0.f ? 0.f : 0.5f, 0.f, 1.f);
        // Note: patched imgui uses g.Style.LayoutAlign; we fall back to 0.5 (centred).

    // Advance cursor using alignment offset computed from previous frame's size.
    item.align_offset = calc_alignment_offset(l, item);
    if (item.align > 0.f)
    {
        if (l.type == LayoutType::Horizontal)
        {
            w->DC.CursorPos.y += item.align_offset;
        }
        else
        {
            float new_x = w->DC.CursorPos.x + item.align_offset;
            signed_indent(item.align_offset);
            w->DC.CursorPos.x = new_x;
        }
    }

    item.measured    = ImRect(w->DC.CursorPos, w->DC.CursorPos);
    item.vtx_begin   = ImGui::GetWindowDrawList()->_VtxCurrentIdx;
    item.vtx_end     = item.vtx_begin;
}

static void end_layout_item(Layout& l)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow*  w = g.CurrentWindow;

    assert(l.current_item_idx < (int)l.items.size());
    LayoutItem& item = l.items[l.current_item_idx];

    item.vtx_end = ImGui::GetWindowDrawList()->_VtxCurrentIdx;

    // Undo vertical indent for vertical layouts.
    if (item.align > 0.f && l.type == LayoutType::Vertical)
        signed_indent(-item.align_offset);

    // Fix alignment if item size changed this frame.
    float new_offset = calc_alignment_offset(l, item);
    if (new_offset != item.align_offset)
    {
        float delta = new_offset - item.align_offset;
        ImVec2 correction = (l.type == LayoutType::Horizontal)
                            ? ImVec2(0.f, delta) : ImVec2(delta, 0.f);
        translate_item(item, correction);
        item.measured.Min += correction;
        item.measured.Max += correction;
        item.align_offset = new_offset;
    }

    // Reset cursor to layout axis.
    if (l.type == LayoutType::Horizontal)
        w->DC.CursorPos.y = l.start_pos.y;
    else
        w->DC.CursorPos.x = l.start_pos.x;

    l.current_item_idx++;
}

// ---------------------------------------------------------------------------
// Spring
// ---------------------------------------------------------------------------

static void add_spring(Layout& l, float weight, float spacing)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow*  w = g.CurrentWindow;

    // Snap cursor to end of previous item (remove item spacing gap).
    {
        LayoutItem& prev = l.items[l.current_item_idx];
        if (l.type == LayoutType::Horizontal)
            w->DC.CursorPos.x = prev.measured.Max.x;
        else
            w->DC.CursorPos.y = prev.measured.Max.y;
    }

    end_layout_item(l);

    LayoutItem& spring = generate_item(l, LayoutItem::Spring);
    spring.measured.Min = spring.measured.Max = w->DC.CursorPos;

    if (weight < 0.f) weight = 0.f;
    spring.spring_weight = weight;

    if (spacing < 0.f)
        spacing = (l.type == LayoutType::Horizontal)
                  ? g.Style.ItemSpacing.x : g.Style.ItemSpacing.y;
    spring.spring_spacing = spacing;

    // Draw a Dummy using the spring size from the previous frame.
    if (spring.spring_size > 0.f || spring.spring_spacing > 0.f)
    {
        ImVec2 spring_spacing_vec, spring_size_vec;
        if (l.type == LayoutType::Horizontal)
        {
            spring_spacing_vec = ImVec2(0.f, g.Style.ItemSpacing.y);
            spring_size_vec    = ImVec2(spring.spring_spacing + spring.spring_size, l.current_size.y);
        }
        else
        {
            spring_spacing_vec = ImVec2(g.Style.ItemSpacing.x, 0.f);
            spring_size_vec    = ImVec2(l.current_size.x, spring.spring_spacing + spring.spring_size);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImFloor(spring_spacing_vec));
        ImGui::Dummy(ImFloor(spring_size_vec));
        ImGui::PopStyleVar();
    }

    l.current_item_idx++;

    begin_layout_item(l);
}

// ---------------------------------------------------------------------------
// Spring balancing
// ---------------------------------------------------------------------------

static void balance_springs(Layout& l)
{
    float total_weight = 0.f;
    int   last_spring  = -1;
    for (int i = 0; i < (int)l.items.size(); ++i)
    {
        if (l.items[i].kind == LayoutItem::Spring)
        {
            total_weight += l.items[i].spring_weight;
            last_spring   = i;
        }
    }

    const bool  horiz     = (l.type == LayoutType::Horizontal);
    const float occupied  = horiz ? l.minimum_size.x : l.minimum_size.y;
    const float available = horiz ? l.current_size.x  : l.current_size.y;
    const float free      = std::max(available - occupied, 0.f);

    float span_start    = 0.f;
    float current_w     = 0.f;
    for (int i = 0; i < (int)l.items.size(); ++i)
    {
        LayoutItem& item = l.items[i];
        if (item.kind != LayoutItem::Spring) continue;

        float old_size = item.spring_size;
        if (free > 0.f && total_weight > 0.f)
        {
            float next_w    = current_w + item.spring_weight;
            float span_end  = std::floor((i == last_spring) ? free : (free * next_w / total_weight));
            item.spring_size = span_end - span_start;
            span_start       = span_end;
            current_w        = next_w;
        }
        else
        {
            item.spring_size = 0.f;
        }

        float delta = item.spring_size - old_size;
        if (delta != 0.f)
        {
            ImVec2 offset = horiz ? ImVec2(delta, 0.f) : ImVec2(0.f, delta);
            item.measured.Max += offset;
            for (int j = i + 1; j < (int)l.items.size(); ++j)
            {
                translate_item(l.items[j], offset);
                l.items[j].measured.Min += offset;
                l.items[j].measured.Max += offset;
            }
        }
    }
}

static void balance_items_alignment(Layout& l)
{
    for (LayoutItem& item : l.items)
    {
        if (item.align <= 0.f) continue;
        float new_offset = calc_alignment_offset(l, item);
        if (new_offset == item.align_offset) continue;
        float delta = new_offset - item.align_offset;
        ImVec2 correction = (l.type == LayoutType::Horizontal)
                            ? ImVec2(0.f, delta) : ImVec2(delta, 0.f);
        translate_item(item, correction);
        item.measured.Min += correction;
        item.measured.Max += correction;
        item.align_offset = new_offset;
    }
}

static ImVec2 calc_layout_size(const Layout& l, bool collapse_springs)
{
    ImVec2 bounds = {};
    const bool horiz = (l.type == LayoutType::Horizontal);
    for (const LayoutItem& item : l.items)
    {
        ImVec2 sz = item.measured.GetSize();
        if (item.kind == LayoutItem::Item)
        {
            if (horiz) { bounds.x += sz.x; bounds.y = std::max(bounds.y, sz.y); }
            else        { bounds.y += sz.y; bounds.x = std::max(bounds.x, sz.x); }
        }
        else // Spring
        {
            float sp = std::floor(item.spring_spacing);
            if (horiz) { bounds.x += sp; if (!collapse_springs) bounds.x += item.spring_size; }
            else        { bounds.y += sp; if (!collapse_springs) bounds.y += item.spring_size; }
        }
    }
    return bounds;
}

static bool has_nonzero_spring(const Layout& l)
{
    for (const LayoutItem& item : l.items)
        if (item.kind == LayoutItem::Spring && item.spring_weight > 0.f)
            return true;
    return false;
}

static void balance_children(Layout& l)
{
    for (Layout* child = l.first_child; child; child = child->next_sibling)
    {
        // Propagate parent size to auto-sized children.
        if (child->type == LayoutType::Horizontal && child->requested_size.x <= 0.f)
            child->current_size.x = l.current_size.x;
        else if (child->type == LayoutType::Vertical && child->requested_size.y <= 0.f)
            child->current_size.y = l.current_size.y;

        balance_children(*child);

        // Expand parent item bounds to fill the space springs consumed.
        if (has_nonzero_spring(*child) && l.current_item_idx > 0)
        {
            LayoutItem& parent_item = l.items[child->parent_item_idx];
            if (child->type == LayoutType::Horizontal && child->requested_size.x <= 0.f)
                parent_item.measured.Max.x = std::max(parent_item.measured.Max.x,
                    parent_item.measured.Min.x + l.current_size.x);
            else if (child->type == LayoutType::Vertical && child->requested_size.y <= 0.f)
                parent_item.measured.Max.y = std::max(parent_item.measured.Max.y,
                    parent_item.measured.Min.y + l.current_size.y);
        }
    }
    balance_springs(l);
    balance_items_alignment(l);
}

// ---------------------------------------------------------------------------
// BeginLayout / EndLayout
// ---------------------------------------------------------------------------

static void begin_layout(ImGuiID id, LayoutType type, ImVec2 size, float align)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow*  w = g.CurrentWindow;

    ImGui::PushID((int)id);

    Layout* l = find_or_create(id, type);
    assert(!l->live && "Layout with this ID is already open. Use PushID() to disambiguate.");
    l->live = true;

    push_layout(l);

    l->requested_size = size;
    l->align          = (align < 0.f) ? -1.f : ImClamp(align, 0.f, 1.f);
    l->current_item_idx = 0;

    l->current_size.x = size.x > 0.f ? size.x : l->minimum_size.x;
    l->current_size.y = size.y > 0.f ? size.y : l->minimum_size.y;

    l->start_pos        = w->DC.CursorPos;
    l->start_cursor_max = w->DC.CursorMaxPos;

    // Splitter: channel 1 = content, channel 0 = clip-rect overlay.
    l->splitter.Split(w->DrawList, 2);
    l->splitter.SetCurrentChannel(w->DrawList, 1);

    ImVec2 clip_max = {
        size.x > 0.f ? l->start_pos.x + size.x : FLT_MAX,
        size.y > 0.f ? l->start_pos.y + size.y : FLT_MAX
    };
    ImGui::PushClipRect(l->start_pos, clip_max, true);

    if (type == LayoutType::Vertical)
    {
        // Push a zero Dummy to initialise the cursor.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
        ImGui::Dummy(ImVec2(0.f, 0.f));
        ImGui::PopStyleVar();

        l->indent = l->start_pos.x - w->DC.CursorPos.x;
        signed_indent(l->indent);
    }

    begin_layout_item(*l);
}

static void end_layout(LayoutType type)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow*  w = g.CurrentWindow;

    assert(g_current && g_current->type == type);
    Layout& l = *g_current;

    end_layout_item(l);

    if (l.current_item_idx < (int)l.items.size())
        l.items.resize(l.current_item_idx);

    if (type == LayoutType::Vertical)
        signed_indent(-l.indent);

    pop_layout();

    // Compute auto sizes.
    const bool auto_w = l.requested_size.x <= 0.f;
    const bool auto_h = l.requested_size.y <= 0.f;

    ImVec2 new_size = l.requested_size;
    if (auto_w) new_size.x = l.current_size.x;
    if (auto_h) new_size.y = l.current_size.y;

    ImVec2 new_min = calc_layout_size(l, true);
    if (new_min.x != l.minimum_size.x || new_min.y != l.minimum_size.y)
    {
        l.minimum_size = new_min;
        if (auto_w) new_size.x = new_min.x;
        if (auto_h) new_size.y = new_min.y;
    }
    if (!auto_w) new_size.x = l.requested_size.x;
    if (!auto_h) new_size.y = l.requested_size.y;

    l.current_size  = new_size;
    ImVec2 measured = new_size;

    // If we have a parent, propagate and balance now.
    if ((auto_w || auto_h) && l.parent)
    {
        if (l.type == LayoutType::Horizontal && auto_w && l.parent->current_size.x > 0.f)
            l.current_size.x = l.parent->current_size.x;
        else if (l.type == LayoutType::Vertical && auto_h && l.parent->current_size.y > 0.f)
            l.current_size.y = l.parent->current_size.y;
        balance_springs(l);
        measured = l.current_size;
    }

    l.current_size  = new_size;
    l.measured_size = measured;

    // Emit as a regular item so the parent layout accounts for our bounds.
    ImVec2 item_max = {};
    if (g_current && g_cur_item)
        item_max = ImMax(g_cur_item->measured.Max, l.start_pos + new_size);

    w->DC.CursorPos    = l.start_pos;
    w->DC.CursorMaxPos = l.start_cursor_max;
    ImGui::ItemSize(new_size);
    ImGui::ItemAdd(ImRect(l.start_pos, l.start_pos + measured), 0);

    if (g_current && g_cur_item)
        g_cur_item->measured.Max = item_max;

    // If this was a root layout, recurse through children to balance springs.
    if (!l.parent)
        balance_children(l);

    // Restore clip rect and merge splitter.
    ImGui::PopClipRect();
    if (!l.parent)
    {
        // Apply final clip rects to content channel draw commands.
        ImVec4 clip = { l.start_pos.x, l.start_pos.y,
                        l.start_pos.x + l.measured_size.x,
                        l.start_pos.y + l.measured_size.y };
        l.splitter.SetCurrentChannel(w->DrawList, 0);
        for (ImDrawCmd& cmd : l.splitter._Channels[1]._CmdBuffer)
        {
            cmd.ClipRect.x = std::max(cmd.ClipRect.x, clip.x);
            cmd.ClipRect.y = std::max(cmd.ClipRect.y, clip.y);
            cmd.ClipRect.z = std::min(cmd.ClipRect.z, clip.z);
            cmd.ClipRect.w = std::min(cmd.ClipRect.w, clip.w);
        }
    }
    l.splitter.Merge(w->DrawList);

    l.live = false;

    ImGui::PopID();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void nb::ui::BeginHorizontal(const char* str_id, ImVec2 size, float align)
{
    begin_layout(ImGui::GetID(str_id), LayoutType::Horizontal, size, align);
}
void nb::ui::BeginHorizontal(const void* ptr_id, ImVec2 size, float align)
{
    begin_layout(ImGui::GetID(ptr_id), LayoutType::Horizontal, size, align);
}
void nb::ui::BeginHorizontal(int id, ImVec2 size, float align)
{
    begin_layout(ImGui::GetID((void*)(intptr_t)id), LayoutType::Horizontal, size, align);
}
void nb::ui::EndHorizontal()
{
    end_layout(LayoutType::Horizontal);
}

void nb::ui::BeginVertical(const char* str_id, ImVec2 size, float align)
{
    begin_layout(ImGui::GetID(str_id), LayoutType::Vertical, size, align);
}
void nb::ui::BeginVertical(const void* ptr_id, ImVec2 size, float align)
{
    begin_layout(ImGui::GetID(ptr_id), LayoutType::Vertical, size, align);
}
void nb::ui::BeginVertical(int id, ImVec2 size, float align)
{
    begin_layout(ImGui::GetID((void*)(intptr_t)id), LayoutType::Vertical, size, align);
}
void nb::ui::EndVertical()
{
    end_layout(LayoutType::Vertical);
}

void nb::ui::Spring(float weight, float spacing)
{
    assert(g_current && "Spring() called outside a layout");
    add_spring(*g_current, weight, spacing);
}
