local cam_eid = entity_find("camera")
if cam_eid then
    local cam = get_camera(cam_eid)
    _G.CAMERA_EID = cam_eid
    engine:clear_render_layers()
    local rl      = render_layer.new()
    rl.order      = 0
    rl.camera     = cam_eid
    rl.follow_ui  = true
    rl.clear      = true
    rl.clear_r    = 0.3
    rl.clear_g    = 0.3
    rl.clear_b    = 0.3
    engine:add_render_layer(rl)

    local rl2      = render_layer.new()
    rl2.order      = 1
    rl2.camera     = cam_eid
    rl2.follow_ui  = false
    rl2.clear      = true
    rl2.clear_r    = 1.0
    rl2.clear_g    = 0.0
    rl2.clear_b    = 1.0
    rl2.viewport.x = 200
    rl2.viewport.y = 200
    rl2.viewport.w = 400
    rl2.viewport.h = 400
    engine:add_render_layer(rl2)
end

audio_bgm_play(hs("res/etc/navinhas loca.ogg"))
