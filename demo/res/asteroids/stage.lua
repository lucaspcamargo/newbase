-- stage script

physics2d_set_gravity(vec2.new(0.0, 0.0))

render_simple_cam_2d_setup(0, 0, 1920, 1080)

if sys_audio then
    audio_bgm_play(hs("res/asteroids/bgm/ObservingTheStar/ObservingTheStar.ogg"))
else
    print("[stage.lua] no audio service? skip bgm")
end

-- entity tracking tables (eid -> true)
local asteroid_entities = {}
local shot_entities     = {}

local ASTEROID_ETREE = hs("res/asteroids/asteroid.et.yaml")
local SHOT_ETREE     = hs("res/asteroids/shot.et.yaml")

local function spawn_asteroid(x, y, vx, vy)
    local eid = entity_spawn(ASTEROID_ETREE)
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

-- spawn some initial asteroids
spawn_asteroid(-600, -300,  0.3,  0.5)
spawn_asteroid( 500, -250, -0.4,  0.3)
spawn_asteroid(-400,  350,  0.2, -0.6)
spawn_asteroid( 600,  300, -0.5, -0.2)
spawn_asteroid(  50, -450,  0.6,  0.1)

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

        local shot_hit_asteroid = (shot_entities[a] and asteroid_entities[b])
        local asteroid_hit_shot = (shot_entities[b] and asteroid_entities[a])

        if shot_hit_asteroid then
            entity_destroy(a)
            entity_destroy(b)
            shot_entities[a]     = nil
            asteroid_entities[b] = nil
            if sys_audio then audio_sfx_play(hs("res/asteroids/sfx/good.ogg")) end
        elseif asteroid_hit_shot then
            entity_destroy(b)
            entity_destroy(a)
            shot_entities[b]     = nil
            asteroid_entities[a] = nil
            if sys_audio then audio_sfx_play(hs("res/asteroids/sfx/good.ogg")) end
        end
    end
end)
