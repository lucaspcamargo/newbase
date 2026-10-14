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
rl.clear_g    = 0.05
rl.clear_b    = 0.05
rl.three_dee  = true
engine:add_render_layer(rl)


-- ambient sound
audio_bgm_play(hs("res/hello_3d/wind-chimes-day-ambience.ogg"))
audio_bgm_gain(-3.0)

-- rotation anim
local ent_helmet = entity_find("mesh")
time = 0
last_time_interact = 0;
rot_spd = 0.0;
local update_handle = clock_update_add(function(delta)
    time = time + delta
    local sp = get_spatial(ent_helmet)
    if sp then
        local dir = input_action_direction(hs("dir"))
        if dir.x ~= 0.0 then
            sp.rot.z = sp.rot.z - delta * dir.x * 150
            last_time_interact = time
        end
        if dir.y ~= 0.0 then
            sp.rot.x = sp.rot.x + delta * dir.y * 150
            last_time_interact = time
        end
        if time > last_time_interact + 2 then
            if rot_spd < 90 then
                rot_spd = math.min(90, rot_spd + delta*90)
            end
        else
--             if rot_spd > 0 then
--                 rot_spd = math.max(0, rot_spd - delta*90)
--             end
            rot_spd = 0.0
        end

        sp.rot.z = sp.rot.z + rot_spd*delta

    end
end)

script_on_destroy(function()
clock_update_remove(update_handle)
end)
