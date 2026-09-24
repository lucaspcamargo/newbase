-- title screen: wait a couple of seconds, then switch to gameplay
local DELAY   = 3.0
local timer   = 0
local changed = false

-- setup render layer
local cam_eid = entity_find("camera")
if cam_eid then
    engine:clear_render_layers()
    local cam = get_camera(cam_eid)
    _G.CAMERA_EID = cam_eid
    local rl      = render_layer.new()
    rl.order      = 0
    rl.camera     = cam_eid
    rl.follow_ui  = true
    rl.ui_overlays = true
    rl.clear      = true
    rl.clear_r    = 0
    rl.clear_g    = 0
    rl.clear_b    = 0
    engine:add_render_layer(rl)
    end


local update_handle = clock_update_add(function(delta)
    if changed then return end
    timer = timer + delta
    if timer >= DELAY then
        changed = true
        scene_load(hs("res/asteroids/gameplay.et.yaml"))
    end
end)

script_on_destroy(function()
    clock_update_remove(update_handle)
end)
