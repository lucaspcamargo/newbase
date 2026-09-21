-- Demonstrates the lupi system's RTTI-exposed lifecycle controls: this scene
-- just boots a cart when entered. lupi:on_scene_change() stops it again
-- automatically when you switch to a different demo.

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
    rl.clear_r    = 0.05
    rl.clear_g    = 0.05
    rl.clear_b    = 0.05
    engine:add_render_layer(rl)
    end

input_set_overlay_dpad(true)

lupi_start("res/lupi_demo/tiny_quest/lupi.yaml")

script_on_destroy(function()
    input_set_overlay_dpad(false)
end)
