-- map is 60x20 tiles @ 70x70px = 4200x1400
local MAP_W = 60 * 70
local MAP_H = 20 * 70

render_simple_set_clear_color(0.82, 0.96, 0.97)

-- default gravity
physics2d_reset_gravity()

-- Play bgm, looped
audio_bgm_play(hs("res/platformer/bgm/Grasslands Theme.ogg"))

-- Spawn world camera and register world render layer
local CAMERA_ETREE = hs("res/platformer/camera.et.yaml")
local _res_camera  = res_get_etree(CAMERA_ETREE)
local cam_eid      = entity_spawn(CAMERA_ETREE)
if cam_eid then
    local cam = get_camera(cam_eid)
    if cam then
        cam.zoom = math.min(render_simple_window_width()  / 1920,
                            render_simple_window_height() / 1080)
    end
    local sp = get_spatial(cam_eid)
    if sp then
        sp.pos = vec3.new(MAP_W * 0.5, MAP_H * 0.5, 0)
        sp:apply()
    end
    _G.CAMERA_EID = cam_eid
    engine:clear_render_layers()
    local rl      = render_layer.new()
    rl.order      = 0
    rl.layer_mask = 0x1   -- world layer
    rl.camera     = cam_eid
    rl.viewport   = render_simple_default_viewport()
    engine:add_render_layer(rl)
end

-- Spawn HUD (camera + script, defined in one etree)
local HUD_ETREE = hs("res/platformer/hud.et.yaml")
local _res_hud  = res_get_etree(HUD_ETREE)
entity_spawn(HUD_ETREE)
local hud_cam_eid = entity_find("hud_camera")
if hud_cam_eid then
    local sp = get_spatial(hud_cam_eid)
    if sp then
        sp.pos = vec3.new(render_simple_window_width() * 0.5, render_simple_window_height() * 0.5, 0)
        sp:apply()
    end
    _G.HUD_CAMERA_EID = hud_cam_eid
    local rl      = render_layer.new()
    rl.order      = 1
    rl.layer_mask = 0x2   -- HUD layer
    rl.camera     = hud_cam_eid
    rl.viewport   = render_simple_default_viewport()
    engine:add_render_layer(rl)
end

-- Spawn dynamic objects from the map's object layer
local MAP_RES      = hs("res/platformer/map/map_0.tmj")
local COIN_ETREE   = hs("res/platformer/coin.et.yaml")
local PLAYER_ETREE = hs("res/platformer/player.et.yaml")
local ENEMY_ETREE  = hs("res/platformer/enemy.et.yaml")

local _res_coin   = res_get_etree(COIN_ETREE)
local _res_player = res_get_etree(PLAYER_ETREE)
local _res_enemy  = res_get_etree(ENEMY_ETREE)

local etree_for_type = {
    coin   = COIN_ETREE,
    player = PLAYER_ETREE,
    enemy  = ENEMY_ETREE,
}

local ts = sys_tilemap_system
if ts then
    local count = ts:get_layer_object_count(MAP_RES, "objects")
    for i = 0, count - 1 do
        local obj = ts:get_layer_object(MAP_RES, "objects", i)
        local etree = etree_for_type[obj.type]
        if etree then
            local e = entity_spawn(etree)
            if e then
                local sp = get_spatial(e)
                if sp then
                    local cx, cy
                    if obj.shape == 1 then          -- SHAPE_RECTANGLE: origin is top-left
                        cx = obj.x + obj.width  * 0.5
                        cy = obj.y + obj.height * 0.5
                    elseif obj.shape == 2 then      -- SHAPE_TILE: origin is bottom-left
                        cx = obj.x + obj.width  * 0.5
                        cy = obj.y - obj.height * 0.5
                    else                            -- SHAPE_POINT: origin is the point
                        cx = obj.x
                        cy = obj.y
                    end
                    sp.pos = vec3.new(cx, cy, 0)
                    sp:apply()
                end
            end
        end
    end
end

script_on_destroy(function()
    engine:clear_render_layers()
end)
