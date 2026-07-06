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
