local dir_hs  = hs("dir")
local jump_hs = hs("btn_south")

local WALK_SPEED    = 350.0
local JUMP_VEL      = -620.0
local JUMP_CUT_VEL  = -250.0   -- upward speed clamped to on early release

local MAP_W  = 60 * 70
local MAP_H  = 20 * 70
local VIEW_W = 1920
local VIEW_H = 1080

local SPAWN_X = 490
local SPAWN_Y = 100

local update_handle = clock_update_add(function(delta)
    local dir = input_action_direction(dir_hs)
    local ch  = c_character2d()
    local sp  = c_spatial()
    if not ch or not sp then return end

    -- Snap back to spawn if the player exits the map
    if sp.pos.x < 0 or sp.pos.x > MAP_W or sp.pos.y < 0 or sp.pos.y > MAP_H then
        physics2d_character_warp(eid, vec2.new(SPAWN_X, SPAWN_Y))
        physics2d_character_set_velocity(eid, vec2.new(0, 0))
        return
    end

    local vx = dir.x * WALK_SPEED
    local vy = ch.velocity.y  -- preserve vertical (gravity accumulated by system)

    -- Variable jump height: cut upward velocity when button released early
    if vy < JUMP_CUT_VEL and not input_action_is_pressed(jump_hs) then
        vy = JUMP_CUT_VEL
    end

    if ch.grounded and input_action_was_pressed(jump_hs) then
        vy = JUMP_VEL
    end

    physics2d_character_set_velocity(eid, vec2.new(vx, vy))

    -- Camera follow, clamped so the viewport never shows outside the map
    local half_w = VIEW_W * 0.5
    local half_h = VIEW_H * 0.5
    local cam_x = math.max(half_w, math.min(MAP_W - half_w, sp.pos.x))
    local cam_y = math.max(half_h, math.min(MAP_H - half_h, sp.pos.y))
    render_simple_cam_2d_setup(cam_x, cam_y, VIEW_W, VIEW_H)
end)

script_on_destroy(function()
    clock_update_remove(update_handle)
end)
