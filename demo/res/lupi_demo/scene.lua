-- Demonstrates the lupi system's RTTI-exposed lifecycle controls: this scene
-- just boots a cart when entered. lupi:on_scene_change() stops it again
-- automatically when you switch to a different demo.

local _rs = svc_renderer_service()
if _rs then
    _rs:cam_2d_setup(0, 0, 480, 270)
    _rs:set_clear_color(0.05, 0.05, 0.05)
end

lupi_start("res/lupi_demo/tiny_quest/lupi.yaml")
