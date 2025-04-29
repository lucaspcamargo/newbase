local accel_hs = hs("btn_south")
local shoot_hs = hs("btn_west")
local dir_hs = hs("dir")
local DRAG = 0.98
local ROT_SPEED = 100.0

local vel = vec3.new()
local thrust = 200

clock_update_add(function (delta)
    local sp = spatial()
    local dir = input_action_direction(dir_hs)
    sp.rot.z = sp.rot.z + delta*ROT_SPEED*dir.x

    vel.x = vel.x - dir.y*delta*math.cos(math.rad(sp.rot.z))*thrust
    vel.y = vel.y - dir.y*delta*math.sin(math.rad(sp.rot.z))*thrust
    if dir.y == 0.0 then
        -- apply drag 👠 (yes, in space 🌌)
        local drag_factor = DRAG ^ (delta/(1.0/60.0))
        vel = vel * drag_factor
    end
    sp.pos = sp.pos + delta*vel

    local w = render_window_width() / render_cam_2d_scale()
    local h = render_window_height() / render_cam_2d_scale()

    if sp.pos.x < (-w)/2 then
        sp.pos.x = sp.pos.x + w
    elseif sp.pos.x > w/2 then
        sp.pos.x = sp.pos.x - w
    end

    if sp.pos.y < (-h)/2 then
        sp.pos.y = sp.pos.y + h
    elseif sp.pos.y > h/2 then
        sp.pos.y = sp.pos.y - h
    end

    sp:apply()
end)