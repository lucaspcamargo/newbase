local dir_hs  = hs("dir")
local jump_hs = hs("btn_south")

local sfx_jump = res_get_vorbis(hs("res/platformer/sfx/jump.ogg"))
local sfx_land = res_get_vorbis(hs("res/platformer/sfx/land.ogg"))

local COLOR = "Beige"
local SEQ_STAND = "alien" .. COLOR .. "_stand"
local SEQ_WALK = "alien" .. COLOR .. "_walk"
local SEQ_JUMP = "alien" .. COLOR .. "_jump"

local WALK_SPEED    = 350.0
local JUMP_VEL      = -620.0
local JUMP_CUT_VEL  = -250.0   -- upward speed clamped to on early release
local TERMINAL_FALL_VEL = 500.0

local MAP_W  = 60 * 70
local MAP_H  = 20 * 70
local VIEW_W = 1920
local VIEW_H = 1080

-- Capture spawn position from wherever scene.lua placed us
local _sp0    = c_spatial()
local SPAWN_X = _sp0 and _sp0.pos.x or 0
local SPAWN_Y = _sp0 and _sp0.pos.y or 0

local was_grounded = false

-- expose eid globally so enemy scripts can find the player
_G.PLAYER_EID = eid

local INVULN_DURATION = 1.5
is_invulnerable       = false   -- in env: readable by enemy scripts
local invuln_timer    = 0.0

function hurt()
    if is_invulnerable then return end
    is_invulnerable = true
    invuln_timer    = INVULN_DURATION
    -- knock player back upward
    local ch = c_character2d()
    local vx = ch and -ch.velocity.x * 0.5 or 0
    physics2d_character_set_velocity(eid, vec2.new(vx, -380.0))
end

function bounce()
    physics2d_character_set_velocity(eid, vec2.new(
        c_character2d() and c_character2d().velocity.x or 0,
        -420.0))
end

local update_handle = clock_update_add(function(delta)
    local dir = input_action_direction(dir_hs)
    local ch  = c_character2d()
    local sp  = c_spatial()
    local spr = c_sprite()

    if not ch or not sp or not spr then return end

    -- Invulnerability countdown
    if is_invulnerable then
        invuln_timer = invuln_timer - delta
        if invuln_timer <= 0 then is_invulnerable = false end
    end

    -- Snap back to spawn if the player exits the map
    if sp.pos.x < 0 or sp.pos.x > MAP_W or sp.pos.y < 0 or sp.pos.y > MAP_H then
        physics2d_character_warp(eid, vec2.new(SPAWN_X, SPAWN_Y))
        physics2d_character_set_velocity(eid, vec2.new(0, 0))
        return
    end

    local vx = dir.x * WALK_SPEED
    local vy = ch.velocity.y  -- preserve vertical (gravity accumulated by system)

    -- Limit vertical velocity
    if vy > TERMINAL_FALL_VEL and not ch.grounded then
        vy = TERMINAL_FALL_VEL
    end

    -- Variable jump height: cut upward velocity when button released early
    if vy < JUMP_CUT_VEL and not input_action_is_pressed(jump_hs) then
        vy = JUMP_CUT_VEL
    end

    if ch.grounded and not was_grounded then
        audio_sfx_play(hs("res/platformer/sfx/land.ogg"), 1.0)
    end

    if ch.grounded and input_action_was_pressed(jump_hs) then
        vy = JUMP_VEL
        audio_sfx_play(hs("res/platformer/sfx/jump.ogg"), 1.0)
    end

    was_grounded = ch.grounded

    physics2d_character_set_velocity(eid, vec2.new(vx, vy))

    -- update sprite
    if ch.grounded then
        if dir.x == 0.0 then
            spr.sequence = SEQ_STAND
        else
            spr.sequence = SEQ_WALK
            if dir.x < 0.0 then
                sp.scale = vec3.new(-1.0, 1.0, 1.0)
            else
                sp.scale = vec3.new(1.0, 1.0, 1.0)
            end
        end
    else
        spr.sequence = SEQ_JUMP
    end

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
