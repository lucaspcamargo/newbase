#include <newbase/physics2d/debug_draw.hpp>
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

void nb::physics2d_pre_debug_draw(b2DebugDraw &draw, float cx, float cy, float scale_x, float scale_y, float world_scale, float ui_scale, float screen_center_x, float screen_center_y)
{
    ImGui::GetBackgroundDrawList();

    sx = scale_x / ui_scale;
    sy = scale_y / ui_scale;

    dx = screen_center_x - cx * sx;
    dy = screen_center_y - cy * sy;

    //log::info("PRE DEBUG DRAW dx=%f, dy=%f, sx=%f, sy=%f", dx, dy, sx, sy);

    // HACK, fix later
    draw.drawingBounds = b2AABB{b2Vec2{-100000000.0f, -100000000.0f}, b2Vec2{100000000.0f, 100000000.0f}};
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