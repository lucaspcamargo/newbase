local shoot_hs = hs("btn_west")
local dir_hs   = hs("dir")

local spr_normal   = res_get_sprite(hs("res/asteroids/spr/ship.sprite"))
local spr_thruster = res_get_sprite(hs("res/asteroids/spr/ship-t.sprite"))

local ROT_TORQUE         = 30000000.0
local THRUST             = 6000000
local THRUST_ANGLE_DELTA = 180

local SHOOT_SPEED    = 800    -- approximate world units/s
local SHOOT_COOLDOWN = 0.25   -- seconds between shots
local SHOOT_OFFSET   = 40     -- pixels ahead of ship centre

local shoot_timer = 0.0

clock_update_add(function(delta)

    local dir = input_action_direction(dir_hs)
    local sp  = c_spatial()

    -- swap sprite based on thrust input
    local spr_comp = c_sprite()
    if spr_comp then
        spr_comp.spr = (dir.y ~= 0) and spr_thruster or spr_normal
    end

    -- thrust & rotation
    local thrust_dir = math.rad(sp.rot.z + THRUST_ANGLE_DELTA)
    local thrust_x   = math.cos(thrust_dir) * THRUST * dir.y
    local thrust_y   = math.sin(thrust_dir) * THRUST * dir.y

    physics2d_body_force_center(eid, vec2.new(thrust_x, thrust_y), false)
    physics2d_body_torque(eid, dir.x * ROT_TORQUE, true)

    -- screen wrapping
    local renderer_svc = svc_renderer_service()
    if renderer_svc then
        local e  = renderer_svc:get_2d_extents()
        local w  = e.xspan
        local h  = e.yspan
        local nx = sp.pos.x
        local ny = sp.pos.y

        if sp.pos.x < -w/2 then nx = sp.pos.x + w
        elseif sp.pos.x > w/2 then nx = sp.pos.x - w end

        if sp.pos.y < -h/2 then ny = sp.pos.y + h
        elseif sp.pos.y > h/2 then ny = sp.pos.y - h end

        if nx ~= sp.pos.x or ny ~= sp.pos.y then
            physics2d_body_warp(eid, vec2.new(nx, ny))
        end
    else
        print("[ship.lua] no renderer service? skip viewport wrapping")
    end

    -- shooting
    shoot_timer = shoot_timer - delta
    if input_action_is_pressed(shoot_hs) and shoot_timer <= 0.0 and stage_spawn_shot then
        shoot_timer = SHOOT_COOLDOWN
        -- ship nose points opposite to thrust direction
        local nose_angle = sp.rot.z + THRUST_ANGLE_DELTA + 180
        local rad = math.rad(nose_angle)
        local ox  = math.cos(rad) * SHOOT_OFFSET
        local oy  = math.sin(rad) * SHOOT_OFFSET
        stage_spawn_shot(sp.pos.x + ox, sp.pos.y + oy, nose_angle, SHOOT_SPEED)
        if sys_audio then audio_sfx_play(hs("res/asteroids/sfx/blasteroids/bullet-laser.ogg"), -4.0) end
    end

end)
