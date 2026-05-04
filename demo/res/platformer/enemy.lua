local WALK_SPEED     = 90.0
local STOMP_VEL_Y    = 120.0   -- minimum downward player velocity to count as stomp
local STOMP_TOLERANCE = 10.0   -- px: player feet can be this far below enemy top and still stomp

-- capsule dims (must match enemy.et.yaml)
local E_HR = 12.0
local E_HH = 6.0

-- player capsule dims (must match player.et.yaml)
local P_HR = 16.0
local P_HH = 30.0

local dir       = 1     -- 1 = right, -1 = left
alive           = true  -- in env: scene.lua can read this if needed

local patrol_handle = nil
local die_handle    = nil
local die_timer     = 0.0

local function check_player_collision()
    local player_eid = _G.PLAYER_EID
    if not player_eid then return end

    local p_sp = get_spatial(player_eid)
    local p_ch = get_character2d(player_eid)
    if not p_sp or not p_ch then return end

    local sp = c_spatial()
    if not sp then return end

    local dx = math.abs(p_sp.pos.x - sp.pos.x)
    local dy = math.abs(p_sp.pos.y - sp.pos.y)

    -- broad AABB check
    if dx >= P_HR + E_HR or dy >= P_HH + P_HR + E_HH + E_HR then return end

    local p_env = script_get_env(player_eid)
    if not p_env or p_env.is_invulnerable then return end

    -- stomp: player falling fast enough and player feet at or above enemy top
    local player_feet = p_sp.pos.y + P_HH + P_HR
    local enemy_top   = sp.pos.y - E_HH - E_HR
    if p_ch.velocity.y >= STOMP_VEL_Y and player_feet <= enemy_top + STOMP_TOLERANCE then
        if p_env.bounce then p_env.bounce() end
        stomp()
    else
        if p_env.hurt then p_env.hurt() end
    end
end

function stomp()
    if not alive then return end
    alive = false
    clock_update_remove(patrol_handle)
    local spr = c_sprite()
    if spr then spr.sequence = "slimeGreen_squashed" end
    die_timer = 0.4
    die_handle = clock_update_add(function(dt)
        die_timer = die_timer - dt
        if die_timer <= 0 then entity_destroy(eid) end
    end)
end

patrol_handle = clock_update_add(function(delta)
    if not alive then return end

    local ch = c_character2d()
    local sp = c_spatial()
    if not ch or not sp then return end

    local flipped = false

    -- edge detection: ray starts from entity center (above tile surface) to avoid
    -- starting inside the shape, which causes CastRayClosest to return no-hit
    if ch.grounded then
        local ray_x  = sp.pos.x + dir * (E_HR + 8)
        local ray_y0 = sp.pos.y                        -- entity center, above feet
        local ray_y1 = sp.pos.y + E_HH + E_HR + 48    -- well below feet
        local ray    = physics2d_raycast(ray_x, ray_y0, ray_x, ray_y1, 1)
        if ray.x < 0 then
            dir = -dir
            flipped = true
        end
    end

    -- wall detection: velocity clipped near zero = hit a wall; skip if edge already flipped
    if not flipped and ch.grounded and math.abs(ch.velocity.x) < WALK_SPEED * 0.1 then
        dir = -dir
    end

    physics2d_character_set_velocity(eid, vec2.new(dir * WALK_SPEED, ch.velocity.y))

    -- face direction of travel
    local spr = c_sprite()
    if spr then
        sp.scale = vec3.new(-dir, 1.0, 1.0)
        sp:apply()
    end

    check_player_collision()
end)

script_on_destroy(function()
    clock_update_remove(patrol_handle)
    if die_handle then clock_update_remove(die_handle) end
end)
