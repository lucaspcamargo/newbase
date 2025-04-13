

local accel_hs = hs("btn_south")
local shoot_hs = hs("btn_west")
local dir_hs = hs("dir")

clock_update_add(function (delta)
    local sp = spatial()
    local dir = input_action_direction(dir_hs)
    sp.rot.z = sp.rot.z + delta*50.0*dir.x
    print(dir.x)
    if input_action_is_pressed(accel_hs) then
        -- apply acceleration
    else
        -- apply drag 👠
    end
    sp:apply()
end)