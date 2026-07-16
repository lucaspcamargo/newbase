local _rs = svc_renderer_service()
if _rs then
    _rs:cam_2d_setup(0, 0, 1920, 1080)
    _rs:set_clear_color(0.373, 0.553, 0.827)
end
physics2d_set_gravity(vec2.new(0, 200))

local BALL_ETREE = hs("res/physics2d_demo/ball.et.yaml")
local BOX_ETREE  = hs("res/physics2d_demo/box.et.yaml")

-- spawn a mix of circles and boxes scattered inside the container
local spawns = {
    { BALL_ETREE,  -180,  -80 },
    { BALL_ETREE,    60, -120 },
    { BALL_ETREE,   200,  -60 },
    { BALL_ETREE,  -100,   40 },
    { BALL_ETREE,   120,   80 },
    { BOX_ETREE,   -220,  -140 },
    { BOX_ETREE,    -20,  -160 },
    { BOX_ETREE,    180,  -150 },
    { BOX_ETREE,   -150,   60 },
    { BOX_ETREE,     80,   20 },
}

for _, s in ipairs(spawns) do
    local eid = entity_spawn(s[1])
    if eid then
        physics2d_body_warp(eid, vec2.new(s[2], s[3]))
        physics2d_body_set_angular_velocity(eid, math.rad((math.random() * 2 - 1) * 180))
    end
end

-- mouse/touch dragging, in the spirit of the classic Box2D "mouse joint" samples.
-- input_pointer_* is a single mouse-or-touch pointer; physics2d_point_query finds the
-- topmost body under it, and physics2d_drag_* drives a script-owned motor joint to follow it.

local function screen_to_world(px, py)
    local rs = svc_renderer_service()
    if not rs then return vec2.new(0, 0) end
    local e = rs:get_2d_extents()

    -- extents' width/height/screen_x/screen_y are physical pixels; ui_scale converts
    -- them to the same logical space that mouse/touch coordinates are reported in.
    local ox = e.screen_x / e.ui_scale
    local oy = e.screen_y / e.ui_scale
    local w  = e.width    / e.ui_scale
    local h  = e.height   / e.ui_scale

    local fx = w > 0 and (px - ox) / w or 0
    local fy = h > 0 and (py - oy) / h or 0
    return vec2.new(e.left + fx * (e.right - e.left), e.top + fy * (e.bottom - e.top))
end

local drag_id = nil

clock_update_add(function(delta)
    local ppos = input_pointer_position()
    local wpos = screen_to_world(ppos.x, ppos.y)

    if input_pointer_was_pressed() and not drag_id then
        local eid = physics2d_point_query(wpos, 0xffffffffffffffff)
        if eid ~= ENTITY_NULL then
            drag_id = physics2d_drag_begin(eid, wpos, 100.0)
        end
    elseif drag_id then
        if input_pointer_is_pressed() then
            physics2d_drag_update(drag_id, wpos)
        else
            physics2d_drag_end(drag_id)
            drag_id = nil
        end
    end
end)
