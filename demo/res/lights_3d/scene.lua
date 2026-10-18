local cam_eid = entity_find("camera")

-- setup render layer
local cam = get_camera(cam_eid)
engine:clear_render_layers()
local rl      = render_layer.new()
rl.order      = 0
rl.camera     = cam_eid
rl.follow_ui  = true
rl.clear      = true
rl.clear_r    = 0.0
rl.clear_g    = 0.0
rl.clear_b    = 0.0
rl.three_dee  = true
engine:add_render_layer(rl)

-- rotation anim
local ent_helmet = entity_find("mesh")
local ent_cam = entity_find("camera")
local ent_l_r = entity_find("light_point_r")
local ent_l_g = entity_find("light_point_g")
local ent_l_b = entity_find("light_point_b")
local ent_spot = entity_find("light_spot")

time = 0
rot_spd = -15.0;

local update_handle = clock_update_add(function(delta)
    time = time + delta

    local sp = get_spatial(ent_helmet)
    sp.rot.y = sp.rot.y + rot_spd*delta

    local dir = input_action_direction(hs("dir"))
    local sp_cam = get_spatial(ent_cam)
    sp_cam.pos.x = sp_cam.pos.x + dir.x*delta*5
    sp_cam.pos.z = sp_cam.pos.z + dir.y*delta*5

    local sp_r = get_spatial(ent_l_r)
    local angle_r = time
    sp_r.pos.x = math.sin(angle_r)*3
    sp_r.pos.z = math.cos(angle_r)*3
    sp_r.pos.y = 2

    local sp_g = get_spatial(ent_l_g)
    local angle_g = time + math.rad(120)
    sp_g.pos.x = math.sin(angle_g)*3
    sp_g.pos.z = math.cos(angle_g)*3
    sp_g.pos.y = 2

    local sp_b = get_spatial(ent_l_b)
    local angle_b = time + math.rad(-120)
    sp_b.pos.x = math.sin(angle_b)*3
    sp_b.pos.z = math.cos(angle_b)*3
    sp_b.pos.y = 2

    local sp_spot = get_spatial(ent_spot)
    sp_spot.rot.z = math.sin(time/2.0) * 30
end)

script_on_destroy(function()
clock_update_remove(update_handle)
end)
