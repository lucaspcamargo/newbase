-- map is 60x20 tiles @ 70x70px = 4200x1400
local MAP_W = 60 * 70
local MAP_H = 20 * 70

-- Initial camera centered on map; player.lua will take over each frame
render_simple_cam_2d_setup(MAP_W * 0.5, MAP_H * 0.5, MAP_W, MAP_H)
render_simple_set_clear_color(0.82, 0.96, 0.97)

-- default gravity
physics2d_reset_gravity()

-- Play bgm, looped
audio_bgm_play(hs("res/platformer/bgm/Grasslands Theme.ogg"))

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
