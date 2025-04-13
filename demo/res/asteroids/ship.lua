local accel_hs = hs("btn_south")
local shoot_hs = hs("btn_west")
local dir_hs = hs("dir")

local vel = vec3.new()
local thrust = 150

clock_update_add(function (delta)
    local sp = spatial()
    local dir = input_action_direction(dir_hs)
    sp.rot.z = sp.rot.z + delta*50.0*dir.x

    vel.x = vel.x - dir.y*delta*math.cos(math.rad(sp.rot.z))*thrust
    vel.y = vel.y - dir.y*delta*math.sin(math.rad(sp.rot.z))*thrust
    if input_action_is_pressed(accel_hs) then
        -- apply acceleration
    else
        -- apply drag 👠
    end
    sp.pos = sp.pos + delta*vel

    local w = render_window_width()
    local h = render_window_height()

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