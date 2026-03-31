local accel_hs = hs("btn_south")
local shoot_hs = hs("btn_west")
local dir_hs = hs("dir")

local DRAG = 0.98
local ROT_TORQUE = 30000000.0
local THRUST = 6000000
local THRUST_ANGLE_DELTA = 180

local thruster_spr = hs("res/asteroids/sprites/ship-t.png")

clock_update_add(function (delta)

    local dir = input_action_direction(dir_hs)
    local sp = c_spatial()

    local thrust_dir = math.rad(sp.rot.z + THRUST_ANGLE_DELTA)
    local thrust_x = math.cos(thrust_dir)*THRUST*dir.y
    local thrust_y = math.sin(thrust_dir)*THRUST*dir.y

    physics2d_body_force_center(eid, vec2.new(thrust_x, thrust_y), false) -- reset forces
    physics2d_body_torque(eid, dir.x * ROT_TORQUE, true)  -- awake now

    local renderer_svc = svc_renderer_service()
    if renderer_svc then
        -- we know our viewport info, we know when to warp
        local e = renderer_svc:get_2d_extents()  -- returns extents_2d box
        
        local w = e.xspan
        local h = e.yspan
    
        local new_x = sp.pos.x 
        local new_y = sp.pos.y
    
        if sp.pos.x < (-w)/2 then
            new_x = sp.pos.x + w
        elseif sp.pos.x > w/2 then
            new_x = sp.pos.x - w
        end
    
        if sp.pos.y < (-h)/2 then
            new_y = sp.pos.y + h
        elseif sp.pos.y > h/2 then
            new_y = sp.pos.y - h
        end
    
        if new_x ~= sp.pos.x or new_y ~= sp.pos.y then
            physics2d_body_warp(eid, vec2.new(new_x, new_y))
        end
    
    else
        print("COULD NOT GET VIEWPORT SERVICE")

    end


end)