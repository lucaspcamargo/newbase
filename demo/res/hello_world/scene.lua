local cam_eid = entity_find("camera")

-- setup rendder layer
local cam = get_camera(cam_eid)
_G.CAMERA_EID = cam_eid
engine:clear_render_layers()
local rl      = render_layer.new()
rl.order      = 0
rl.camera     = cam_eid
rl.follow_ui  = true
rl.clear      = true
rl.clear_r    = 0.2
rl.clear_g    = 0.2
rl.clear_b    = 0.2
engine:add_render_layer(rl)

audio_bgm_play(hs("res/etc/navinhas loca.ogg"))
