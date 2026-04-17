-- title screen: wait a couple of seconds, then switch to gameplay
local DELAY   = 3.0
local timer   = 0
local changed = false

-- setup 2d camera
render_simple_cam_2d_setup(0, 0, 1920, 1080)

clock_update_add(function(delta)
    if changed then return end
    timer = timer + delta
    if timer >= DELAY then
        changed = true
        scene_load(hs("res/asteroids/gameplay.et.yaml"))
    end
end)
