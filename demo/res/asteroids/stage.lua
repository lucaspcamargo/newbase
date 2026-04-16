-- stage script

physics2d_set_gravity(vec2.new(0.0, 0.0))

render_simple_cam_2d_setup(0, 0, 1920, 1080)

if sys_audio then
    audio_bgm_play(hs("res/asteroids/bgm/ObservingTheStar/ObservingTheStar.ogg"))
else
    print("[stage.lua] no audio service? skip bgm")
end

-- entity tracking tables (eid -> true)
local asteroid_entities       = {}  -- all asteroids (large + small)
local large_asteroid_entities = {}  -- large ones that split on death
local shot_entities           = {}

local ASTEROID_ETREE       = hs("res/asteroids/asteroid.et.yaml")
local SMALL_ASTEROID_ETREE = hs("res/asteroids/small_asteroid.et.yaml")
local SHOT_ETREE           = hs("res/asteroids/shot.et.yaml")

local function spawn_asteroid(x, y, vx, vy)
    local eid = entity_spawn(ASTEROID_ETREE)
    if not eid then return end
    asteroid_entities[eid]       = true
    large_asteroid_entities[eid] = true
    physics2d_body_warp(eid, vec2.new(x, y))
    physics2d_body_set_velocity(eid, vec2.new(vx, vy))
end

local function spawn_small_asteroid(x, y, vx, vy)
    local eid = entity_spawn(SMALL_ASTEROID_ETREE)
    if not eid then return end
    asteroid_entities[eid] = true
    physics2d_body_warp(eid, vec2.new(x, y))
    physics2d_body_set_velocity(eid, vec2.new(vx, vy))
end

local function spawn_shot(x, y, angle_deg, speed)
    local eid = entity_spawn(SHOT_ETREE)
    if not eid then return end
    shot_entities[eid] = true
    physics2d_body_warp(eid, vec2.new(x, y))
    local rad = math.rad(angle_deg)
    physics2d_body_set_velocity(eid, vec2.new(math.cos(rad)*speed, math.sin(rad)*speed))
end

-- expose spawn_shot so ship.lua can call it
_G.stage_spawn_shot = spawn_shot

-- lives display
local LIVES_MAX            = 3
local LIFE_INDICATOR_ETREE = hs("res/asteroids/life_indicator.et.yaml")
local life_indicators      = {}  -- ordered list, pop from end on death
local lives                = LIVES_MAX

do
    local x0, y0, spacing = -920, -490, 70
    for i = 1, LIVES_MAX do
        local eid = entity_spawn(LIFE_INDICATOR_ETREE)
        if eid then
            local sp = get_spatial(eid)
            if sp then
                sp.pos = vec3.new(x0 + (i - 1) * spacing, y0, 10)
            end
            life_indicators[i] = eid
        end
    end
end

local function lose_life()
    if lives <= 0 then return end
    lives = lives - 1
    local eid = table.remove(life_indicators)  -- remove last
    if eid then entity_destroy(eid) end
    print(string.format("[stage] lives remaining: %d", lives))
end

_G.stage_lose_life = lose_life

local ASTEROID_SPEED = 200

-- spawn some initial asteroids
spawn_asteroid(-600, -300,  0.3*ASTEROID_SPEED,  0.5*ASTEROID_SPEED)
spawn_asteroid( 500, -250, -0.4*ASTEROID_SPEED,  0.3*ASTEROID_SPEED)
spawn_asteroid(-400,  350,  0.2*ASTEROID_SPEED, -0.6*ASTEROID_SPEED)
spawn_asteroid( 600,  300, -0.5*ASTEROID_SPEED, -0.2*ASTEROID_SPEED)
spawn_asteroid(  50, -450,  0.6*ASTEROID_SPEED,  0.1*ASTEROID_SPEED)

-- cull shots that have left the viewport
clock_update_add(function(delta)
    local renderer_svc = svc_renderer_service()
    if not renderer_svc then return end
    local e  = renderer_svc:get_2d_extents()
    local hw = e.xspan / 2
    local hh = e.yspan / 2

    for eid, _ in pairs(shot_entities) do
        local sp = get_spatial(eid)
        if sp and (sp.pos.x < -hw or sp.pos.x > hw or sp.pos.y < -hh or sp.pos.y > hh) then
            entity_destroy(eid)
            shot_entities[eid] = nil
        end
    end
end)

-- contact handling
clock_update_add(function(delta)
    local p2d = sys_physics2d
    if not p2d then return end

    local n = p2d:contact_begins_count()
    for i = 0, n - 1 do
        local a = p2d:contact_begin_a(i)
        local b = p2d:contact_begin_b(i)

        local shot_eid, ast_eid
        if shot_entities[a] and asteroid_entities[b] then
            shot_eid, ast_eid = a, b
        elseif shot_entities[b] and asteroid_entities[a] then
            shot_eid, ast_eid = b, a
        end

        if shot_eid then
            -- split large asteroids into two small ones
            if large_asteroid_entities[ast_eid] then
                local sp = get_spatial(ast_eid)
                if sp then
                    local spd = (math.random() * 0.8 + 0.5) * ASTEROID_SPEED
                    local ang = math.random() * math.pi * 2
                    spawn_small_asteroid(sp.pos.x, sp.pos.y,  math.cos(ang)*spd,  math.sin(ang)*spd)
                    spawn_small_asteroid(sp.pos.x, sp.pos.y, -math.cos(ang)*spd, -math.sin(ang)*spd)
                end
                large_asteroid_entities[ast_eid] = nil
            end

            entity_destroy(shot_eid)
            entity_destroy(ast_eid)
            shot_entities[shot_eid]     = nil
            asteroid_entities[ast_eid]  = nil

            if sys_audio then audio_sfx_play(hs("res/asteroids/sfx/blasteroids/explosion.ogg"), 0.0) end
        end
    end
end)
