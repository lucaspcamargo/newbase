local cam_eid = entity_find("camera")

-- setup render layer
local cam = get_camera(cam_eid)
engine:clear_render_layers()
local rl      = render_layer.new()
rl.order      = 0
rl.camera     = cam_eid
rl.follow_ui  = true
rl.clear      = true
rl.clear_r    = 0.1
rl.clear_g    = 0.2
rl.clear_b    = 0.3
rl.three_dee  = true
engine:add_render_layer(rl)


-- ambient sound
audio_bgm_play(hs("res/hello_3d/wind-chimes-day-ambience.ogg"))
audio_bgm_gain(-3.0)

-- rotation anim
local ent_helmet = entity_find("mesh")
local time = 0
local update_handle = clock_update_add(function(delta)
    time = time + delta
    local sp = get_spatial(ent_helmet)
    if sp then
        sp.rot = vec3.new(90, 0, time * 45)
    end
end)

script_on_destroy(function()
clock_update_remove(update_handle)
end)
