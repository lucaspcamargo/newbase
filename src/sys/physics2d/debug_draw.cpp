#include <newbase/sys/physics2d/debug_draw.hpp>
#include <newbase/ui/imgui_icons.hpp>
#include <newbase/log.hpp>
#include <imgui.h>
#include <vector>

using namespace nb;


/// Draw a closed polygon provided in CCW order.
static void DrawPolygon( const b2Vec2* vertices, int vertexCount, b2HexColor color, void* context );

/// Draw a solid closed polygon provided in CCW order.
static void DrawSolidPolygon( b2Transform transform, const b2Vec2* vertices, int vertexCount, float radius, b2HexColor color, void* context );

/// Draw a circle.
static void DrawCircle( b2Vec2 center, float radius, b2HexColor color, void* context );

/// Draw a solid circle.
static void DrawSolidCircle( b2Transform transform, float radius, b2HexColor color, void* context );

/// Draw a solid capsule.
static void DrawSolidCapsule( b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color, void* context );

/// Draw a line segment.
static void DrawSegment( b2Vec2 p1, b2Vec2 p2, b2HexColor color, void* context );

/// Draw a transform. Choose your own length scale.
static void DrawTransform( b2Transform transform, void* context );

/// Draw a point.
static void DrawPoint( b2Vec2 p, float size, b2HexColor color, void* context );

/// Draw a string in world space
static void DrawString( b2Vec2 p, const char* s, b2HexColor color, void* context );



void nb::physics2d_setup_debug_draw(b2DebugDraw &draw, void* context)
{
    draw.context = context;

    draw.drawShapes = true;
    draw.drawContacts = true;
    draw.drawContactNormals = true;
    draw.drawJoints = true;

    draw.DrawPolygonFcn = DrawPolygon;
    draw.DrawSolidPolygonFcn = DrawSolidPolygon;
    draw.DrawCircleFcn = DrawCircle;
    draw.DrawSolidCircleFcn = DrawSolidCircle;
    draw.DrawSolidCapsuleFcn = DrawSolidCapsule;
    draw.DrawSegmentFcn = DrawSegment;
    draw.DrawTransformFcn = DrawTransform;
    draw.DrawPointFcn = DrawPoint;
    draw.DrawStringFcn = DrawString;
}

static constexpr uint32_t ALPHA_ONE = 0xff000000;
static constexpr uint32_t ALPHA_75 = 0xbf000000;
static constexpr uint32_t ALPHA_25 = 0x40000000;

static float dx {0.0f};
static float dy {0.0f};
static float sx {1.0f};
static float sy {1.0f};
static float line_thickness {1.0f};

void nb::physics2d_pre_debug_draw(b2DebugDraw &draw, float cx, float cy, float world_scale, float ui_scale,  glm::vec4 wb, glm::vec4 ui_vp)
{
    static constexpr auto BORDER_COL = 0x800000ff;

    // clip
    ImVec2 clip_min {ui_vp.x, ui_vp.y};
    ImVec2 clip_max {ui_vp.x+ui_vp.z, ui_vp.y+ui_vp.w};
    auto *dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(clip_min, clip_max);
    dl->AddRect(clip_min, clip_max, BORDER_COL, 0, 4);
    dl->AddRectFilled({clip_min.x + 4, clip_min.y + 4}, {clip_min.x + 98, clip_min.y + 24}, BORDER_COL, 3);
    dl->AddText({clip_min.x + 6, clip_min.y + 6}, 0xffffffff, ICON_FK_BUG" Physics 2D");

    // center of ui viewport
    const float ui_vp_cx = ui_vp.x + ui_vp.z  * .5f;
    const float ui_vp_cy = ui_vp.y + ui_vp.w  * .5f;

    // scale factor from world to ui
    const float scale_x = ui_vp.z/wb.z;
    const float scale_y = ui_vp.w/wb.w;

    sx = scale_x;
    sy = scale_y;

    dx = ui_vp_cx - cx * sx;
    dy = ui_vp_cy - cy * sy;

    const auto phys_world_min = b2Vec2{wb.x * world_scale, wb.y * world_scale};
    const auto phys_world_max = b2Vec2{(wb.x+wb.z)*world_scale, (wb.y+wb.w)*world_scale};
    draw.drawingBounds = b2AABB{phys_world_min, phys_world_max};
}

void nb::physics2d_post_debug_draw()
{
    auto *dl = ImGui::GetBackgroundDrawList();
    dl->PopClipRect();
}

/// Draw a closed polygon provided in CCW order.
static void DrawPolygon( const b2Vec2* vertices, int vertexCount, b2HexColor color, void* context )
{
    auto dl = ImGui::GetBackgroundDrawList();
    std::vector<ImVec2> points;
    points.reserve(vertexCount);
    for(int i = 0; i < vertexCount; i++)
    {
        points.push_back(ImVec2{vertices[i].x*sx + dx, vertices[i].y*sy + dy});
    }
    dl->AddPolyline(points.data(), static_cast<int>(points.size()), color|ALPHA_ONE, ImDrawFlags_Closed, line_thickness);
}

/// Draw a solid closed polygon provided in CCW order.
static void DrawSolidPolygon( b2Transform transform, const b2Vec2* vertices, int vertexCount, float radius, b2HexColor color, void* context )
{
    auto dl = ImGui::GetBackgroundDrawList();
    std::vector<ImVec2> points;
    points.reserve(vertexCount);
    for(int i = 0; i < vertexCount; i++)
    {
        auto transformed = b2TransformPoint(transform, vertices[i]);
        points.push_back(ImVec2{transformed.x*sx + dx, transformed.y*sy + dy});
    }
    dl->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), color|ALPHA_25);
    dl->AddPolyline(points.data(), static_cast<int>(points.size()), color|ALPHA_ONE, ImDrawFlags_Closed, line_thickness);

}

/// Draw a circle.
static void DrawCircle( b2Vec2 center, float radius, b2HexColor color, void* context )
{
    auto dl = ImGui::GetBackgroundDrawList();
    ImVec2 c {center.x*sx + dx, center.y*sy + dy};
    dl->AddCircle(c, radius*sx, color|ALPHA_ONE, 0, line_thickness);
}

/// Draw a solid circle.
static void DrawSolidCircle( b2Transform transform, float radius, b2HexColor color, void* context )
{
    auto dl = ImGui::GetBackgroundDrawList();
    b2Vec2 center = b2TransformPoint(transform, b2Vec2_zero);
    ImVec2 c {center.x*sx + dx, center.y*sy + dy};
    dl->AddCircleFilled(c, radius*sx, color|ALPHA_25);
    dl->AddCircle(c, radius*sx, color|ALPHA_ONE, 0, line_thickness);
}

/// Draw a solid capsule.
static void DrawSolidCapsule( b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color, void* context )
{

}

/// Draw a line segment.
static void DrawSegment( b2Vec2 p1, b2Vec2 p2, b2HexColor color, void* context )
{
    auto dl = ImGui::GetBackgroundDrawList();
    ImVec2 pt1 {p1.x*sx + dx, p1.y*sy + dy};
    ImVec2 pt2 {p2.x*sx + dx, p2.y*sy + dy};
    dl->AddLine(pt1, pt2, color|ALPHA_ONE, line_thickness);
}

/// Draw a transform. Choose your own length scale.
static void DrawTransform( b2Transform transform, void* context )
{

}

/// Draw a point.
static void DrawPoint( b2Vec2 p, float size, b2HexColor color, void* context )
{
    auto dl = ImGui::GetBackgroundDrawList();
    ImVec2 pt {p.x*sx + dx, p.y*sy + dy};
    dl->AddRectFilled(ImVec2{pt.x-line_thickness, pt.y-line_thickness},
        ImVec2{pt.x+line_thickness, pt.y+line_thickness}, color|ALPHA_ONE);
}

/// Draw a string in world space
static void DrawString( b2Vec2 p, const char* s, b2HexColor color, void* context )
{
    auto dl = ImGui::GetBackgroundDrawList();
    ImVec2 pt {p.x*sx + dx, p.y*sy + dy};
    dl->AddText(pt, color|ALPHA_75, s);
}
