-- map is 60x20 tiles @ 70x70px = 4200x1400
local MAP_W = 60 * 70
local MAP_H = 20 * 70

-- Initial camera centered on map; player.lua will take over each frame
render_simple_cam_2d_setup(MAP_W * 0.5, MAP_H * 0.5, MAP_W, MAP_H)
render_simple_set_clear_color(0.15, 0.18, 0.25)

