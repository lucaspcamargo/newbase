local cam_eid = entity_find("camera")

local SPIN_SPEED = 200.0

-- setup render layer
local cam = get_camera(cam_eid)
_G.CAMERA_EID = cam_eid
engine:clear_render_layers()
local rl      = render_layer.new()
rl.order      = 0
rl.camera     = cam_eid
rl.follow_ui  = true
rl.clear      = true
rl.clear_r    = 0.0
rl.clear_g    = 0.0
rl.clear_b    = 0.0
engine:add_render_layer(rl)


local update_handle = clock_update_add( function(dt)
    local ent = entity_find("spinner")
    local sp_ref = get_spatial(ent)
    sp_ref.rot.z = sp_ref.rot.z + dt * SPIN_SPEED
end)


script_on_destroy(function()
    clock_update_remove(update_handle)
end)

