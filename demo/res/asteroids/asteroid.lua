-- asteroid script: screen wrapping and 

-- randomize tint slightly: warm grey-brown range
do
    local spr_comp = c_sprite()
    if spr_comp then
        local r = 0.75 + math.random() * 0.25
        local g = 0.60 + math.random() * 0.25
        local b = 0.45 + math.random() * 0.20
        spr_comp.color = vec4.new(r, g, b, 1)
        spr_comp.frame = math.random(0, 2)
    end
end

do
    local sp = c_spatial()
    if sp then
        sp.rot = vec3.new(0, 0, math.random() * 360)
        sp:apply()
    end
    local max_spin = 180  -- degrees per second
    physics2d_body_set_angular_velocity(eid, math.rad((math.random() * 2 - 1) * max_spin))
end

local update_handle = clock_update_add(function(delta)
    local sp = c_spatial()
    if not sp then return end

    local renderer_svc = svc_renderer_service()
    if not renderer_svc then return end

    local e  = renderer_svc:get_2d_extents()
    local w  = e.xspan
    local h  = e.yspan
    local nx = sp.pos.x
    local ny = sp.pos.y

    if sp.pos.x < -w/2 then nx = sp.pos.x + w
    elseif sp.pos.x > w/2 then nx = sp.pos.x - w end

    if sp.pos.y < -h/2 then ny = sp.pos.y + h
    elseif sp.pos.y > h/2 then ny = sp.pos.y - h end

    if nx ~= sp.pos.x or ny ~= sp.pos.y then
        physics2d_body_warp(eid, vec2.new(nx, ny))
    end
end)

script_on_destroy(function()
    clock_update_remove(update_handle)
end)
