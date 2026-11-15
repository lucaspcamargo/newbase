local cam_eid = entity_find("camera")

-- setup render layer
local cam = get_camera(cam_eid)
engine:clear_render_layers()
local rl      = render_layer.new()
rl.order      = 0
rl.camera     = cam_eid
rl.follow_ui  = true
rl.clear      = true
rl.clear_r    = 0.05
rl.clear_g    = 0.1
rl.clear_b    = 0.2
rl.three_dee  = true
engine:add_render_layer(rl)
