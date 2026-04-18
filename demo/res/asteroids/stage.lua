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
local SHIP_ETREE           = hs("res/asteroids/ship.et.yaml")
local DEBRIS_ETREE         = hs("res/asteroids/debris.et.yaml")
local EXPLOSION_ETREE      = hs("res/asteroids/explosion.et.yaml")
local TITLE_ETREE          = hs("res/asteroids/title.et.yaml")
local STARS_ETREE          = hs("res/asteroids/stars.et.yaml")
local SCORE_DISPLAY_ETREE  = hs("res/asteroids/score_display.et.yaml")

entity_spawn(STARS_ETREE)

-- score
local score             = 0
local score_display_eid = entity_spawn(SCORE_DISPLAY_ETREE)

local function add_score(points)
    score = score + points
    if score_display_eid then
        textext_set_text(score_display_eid, "SCORE: " .. score)
    end
end

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

local function spawn_debris(x, y, count, speed)
    for _ = 1, count do
        local eid = entity_spawn(DEBRIS_ETREE)
        if eid then
            physics2d_body_warp(eid, vec2.new(x, y))
            local spd = speed * (0.4 + math.random() * 0.6)
            local ang = math.random() * math.pi * 2
            physics2d_body_set_velocity(eid, vec2.new(math.cos(ang)*spd, math.sin(ang)*spd))
        end
    end
end

local function spawn_explosion(x, y)
    local eid = entity_spawn(EXPLOSION_ETREE)
    if eid then
        local sp = get_spatial(eid)
        if sp then
            sp.pos = vec3.new(x, y, 5)
            sp:apply()
        end
    end
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

-- ship tracking
local ship_eid      = nil
local dead_timer    = 0.0
local DEAD_DURATION = 3.0

local function spawn_ship()
    ship_eid = entity_spawn(SHIP_ETREE)
end

spawn_ship()

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
                sp:apply()
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
local h_cull = clock_update_add(function(delta)
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
local h_contacts = clock_update_add(function(delta)
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
                add_score(100)
            else
                add_score(50)
            end

            local ast_sp = get_spatial(ast_eid)
            entity_destroy(shot_eid)
            entity_destroy(ast_eid)
            shot_entities[shot_eid]     = nil
            asteroid_entities[ast_eid]  = nil

            if ast_sp then
                spawn_debris(ast_sp.pos.x, ast_sp.pos.y, 10, 200)
                spawn_explosion(ast_sp.pos.x, ast_sp.pos.y)
            end
            if sys_audio then
                local pitch = 0.85 + math.random() * 0.30
                audio_sfx_play_pitched(hs("res/asteroids/sfx/blasteroids/explosion.ogg"), 0.0, pitch)
            end
        end

        -- ship vs asteroid
        if ship_eid then
            local hit_ast = nil
            if a == ship_eid and asteroid_entities[b] then
                hit_ast = b
            elseif b == ship_eid and asteroid_entities[a] then
                hit_ast = a
            end

            local ship_invul = false
            if hit_ast and ship_eid then
                local env = script_get_env(ship_eid)
                if env then ship_invul = env.is_invulnerable end
            end

            if hit_ast and not ship_invul then
                local ship_sp = get_spatial(ship_eid)
                entity_destroy(hit_ast)
                asteroid_entities[hit_ast]       = nil
                large_asteroid_entities[hit_ast]  = nil

                entity_destroy(ship_eid)
                ship_eid   = nil
                dead_timer = DEAD_DURATION
                lose_life()

                if ship_sp then
                    spawn_debris(ship_sp.pos.x, ship_sp.pos.y, 18, 280)
                    spawn_explosion(ship_sp.pos.x, ship_sp.pos.y)
                end
                if sys_audio then
                    local pitch = 0.75 + math.random() * 0.20
                    audio_sfx_play_pitched(hs("res/asteroids/sfx/blasteroids/explosion.ogg"), 0.0, pitch)
                end
            end
        end
    end
end)

-- dead timer: respawn or game over
local h_dead_timer = clock_update_add(function(delta)
    if dead_timer <= 0 then return end
    dead_timer = dead_timer - delta
    if dead_timer <= 0 then
        dead_timer = 0
        if lives <= 0 then
            scene_load(TITLE_ETREE)
        else
            spawn_ship()
        end
    end
end)

script_on_destroy(function()
    clock_update_remove(h_cull)
    clock_update_remove(h_contacts)
    clock_update_remove(h_dead_timer)
end)
