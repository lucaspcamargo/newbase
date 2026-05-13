local dir_hs  = hs("dir")
local jump_hs = hs("btn_south")

local sfx_jump   = res_get_vorbis(hs("res/platformer/sfx/jump.ogg"))
local sfx_land   = res_get_vorbis(hs("res/platformer/sfx/land.ogg"))
local sfx_damage = res_get_vorbis(hs("res/platformer/sfx/damage.ogg"))
local sfx_ladder = {
    hs("res/platformer/sfx/ladder1.ogg"),
    hs("res/platformer/sfx/ladder2.ogg"),
}

local COLOR = "Pink"
local SEQ_STAND   = "alien" .. COLOR .. "_stand"
local SEQ_WALK    = "alien" .. COLOR .. "_walk"
local SEQ_JUMP    = "alien" .. COLOR .. "_jump"
local SEQ_HURT    = "alien" .. COLOR .. "_hurt"
local SEQ_CLIMB   = { "alien" .. COLOR .. "_climb1", "alien" .. COLOR .. "_climb2" }

local WALK_SPEED        = 350.0
local JUMP_VEL          = -620.0
local JUMP_CUT_VEL      = -250.0   -- upward speed clamped to on early release
local TERMINAL_FALL_VEL = 500.0
local CLIMB_SPEED = 160.0

local MAP_RES = hs("res/platformer/map/map_0.tmj")
local MAP_W  = 60 * 70
local MAP_H  = 20 * 70
local VIEW_W = 1920
local VIEW_H = 1080

-- Capture spawn position from wherever scene.lua placed us
local _sp0    = c_spatial()
local SPAWN_X = _sp0 and _sp0.pos.x or 0
local SPAWN_Y = _sp0 and _sp0.pos.y or 0

local was_grounded    = false
local ladder_attached = false
local ladder_sfx_idx  = 1
local ladder_sfx_timer = 0.0
local LADDER_SFX_INTERVAL = 0.22

-- expose eid globally so enemy scripts can find the player
_G.PLAYER_EID    = eid
_G.PLAYER_LIVES  = 3
_G.PLAYER_HP     = 3
_G.PLAYER_SCORE  = 0
_G.PLAYER_COINS  = 0

local MAX_LIVES       = 3
local MAX_HP          = 3
local INVULN_DURATION = 1.5
local DEATH_DURATION  = 1.8
is_invulnerable       = false   -- in env: readable by enemy scripts
local invuln_timer    = 0.0
is_dead               = false   -- in env: readable by enemy scripts
local death_timer     = 0.0

local function do_respawn()
    is_dead         = false
    is_invulnerable = true
    invuln_timer    = INVULN_DURATION
    ladder_attached = false
    physics2d_character_warp(eid, vec2.new(SPAWN_X, SPAWN_Y))
    physics2d_character_set_velocity(eid, vec2.new(0, 0))
    local spr = c_sprite()
    if spr then spr.color = vec4.new(1, 1, 1, 1) end
end

local function do_die()
    is_dead         = true
    is_invulnerable = true
    death_timer     = DEATH_DURATION
    ladder_attached = false
    audio_sfx_play(hs("res/platformer/sfx/damage.ogg"), 1.0)
    physics2d_character_set_velocity(eid, vec2.new(0, -300.0))
    local spr = c_sprite()
    if spr then spr.sequence = SEQ_HURT end
end

function hurt()
    if is_invulnerable or is_dead then return end
    _G.PLAYER_HP = _G.PLAYER_HP - 1
    if _G.PLAYER_HP <= 0 then
        _G.PLAYER_LIVES = _G.PLAYER_LIVES - 1
        if _G.PLAYER_LIVES <= 0 then
            _G.PLAYER_LIVES = MAX_LIVES
            _G.PLAYER_SCORE = 0
            _G.PLAYER_COINS = 0
        end
        _G.PLAYER_HP = MAX_HP
        do_die()
    else
        is_invulnerable = true
        invuln_timer    = INVULN_DURATION
        ladder_attached = false
        audio_sfx_play(hs("res/platformer/sfx/damage.ogg"), 1.0)
        local ch = c_character2d()
        local vx = ch and -ch.velocity.x * 0.5 or 0
        physics2d_character_set_velocity(eid, vec2.new(vx, -380.0))
    end
end

function bounce()
    ladder_attached = false
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

    -- Death countdown: lock input, flicker, then respawn
    if is_dead then
        death_timer = death_timer - delta
        local alpha = 0.5 + 0.25 * math.sin((DEATH_DURATION - death_timer) * math.pi * 8)
        spr.color = vec4.new(1, 1, 1, alpha)
        if death_timer <= 0 then do_respawn() end
        return
    end

    -- Invulnerability countdown + opacity flicker
    if is_invulnerable then
        invuln_timer = invuln_timer - delta
        if invuln_timer <= 0 then
            is_invulnerable = false
            if spr then spr.color = vec4.new(1, 1, 1, 1) end
        else
            local t = INVULN_DURATION - invuln_timer
            local alpha = 0.5 + 0.25 * math.sin(t * math.pi * 10)
            if spr then spr.color = vec4.new(1, 1, 1, alpha) end
        end
    end

    -- Snap back to spawn if the player exits the map
    if sp.pos.x < 0 or sp.pos.x > MAP_W or sp.pos.y < 0 or sp.pos.y > MAP_H then
        physics2d_character_warp(eid, vec2.new(SPAWN_X, SPAWN_Y))
        physics2d_character_set_velocity(eid, vec2.new(0, 0))
        ladder_attached = false
        return
    end

    -- Ladder detection: sample lower on the capsule so the player can climb all the way up
    local ts = sys_tilemap_system
    local sample_y = sp.pos.y + ch.capsule_half_height
    local on_ladder_tile = ts and ts:tile_bool_property_at(
        MAP_RES, vec2.new(sp.pos.x, sample_y), "bg", "ladder") or false

    -- Attach to ladder on up/down input; detach when leaving the tile
    if on_ladder_tile and not ladder_attached and dir.y ~= 0.0 then
        ladder_attached = true
        ladder_sfx_timer = 0.0
    end
    if ladder_attached and not on_ladder_tile then
        ladder_attached = false
    end

    -- Jump off the ladder
    local jumped_off_ladder = false
    if ladder_attached and input_action_was_pressed(jump_hs) then
        ladder_attached  = false
        jumped_off_ladder = true
    end

    ch.on_ladder = ladder_attached

    local vx, vy

    if ladder_attached then
        vx = 0.0
        vy = dir.y * CLIMB_SPEED

        -- Step sounds while moving
        if dir.y ~= 0.0 then
            ladder_sfx_timer = ladder_sfx_timer + delta
            if ladder_sfx_timer >= LADDER_SFX_INTERVAL then
                ladder_sfx_timer = ladder_sfx_timer - LADDER_SFX_INTERVAL
                audio_sfx_play(sfx_ladder[ladder_sfx_idx], 0.7)
                ladder_sfx_idx = (ladder_sfx_idx % 2) + 1
            end
        else
            ladder_sfx_timer = 0.0
        end

        spr.sequence = SEQ_CLIMB[ladder_sfx_idx]
    else
        vx = dir.x * WALK_SPEED
        vy = ch.velocity.y  -- preserve vertical (gravity accumulated by system)

        -- Limit fall speed
        if vy > TERMINAL_FALL_VEL and not ch.grounded then
            vy = TERMINAL_FALL_VEL
        end

        -- Variable jump height: cut upward velocity on early release
        if vy < JUMP_CUT_VEL and not input_action_is_pressed(jump_hs) then
            vy = JUMP_CUT_VEL
        end

        if ch.grounded and not was_grounded then
            audio_sfx_play(hs("res/platformer/sfx/land.ogg"), 1.0)
        end

        if jumped_off_ladder or (ch.grounded and input_action_was_pressed(jump_hs)) then
            vy = JUMP_VEL
            audio_sfx_play(hs("res/platformer/sfx/jump.ogg"), 1.0)
        end

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
    end

    was_grounded = ch.grounded

    physics2d_character_set_velocity(eid, vec2.new(vx, vy))

    -- Camera follow, clamped so the viewport never shows outside the map
    local zoom = math.min(render_simple_window_width() / VIEW_W,
                          render_simple_window_height() / VIEW_H)
    local half_w = render_simple_window_width()  * 0.5 / zoom
    local half_h = render_simple_window_height() * 0.5 / zoom
    local cam_x = math.max(half_w, math.min(MAP_W - half_w, sp.pos.x))
    local cam_y = math.max(half_h, math.min(MAP_H - half_h, sp.pos.y))
    local cam_sp = _G.CAMERA_EID and get_spatial(_G.CAMERA_EID)
    if cam_sp then cam_sp.pos = vec3.new(cam_x, cam_y, 0) end
    local cam_c = _G.CAMERA_EID and get_camera(_G.CAMERA_EID)
    if cam_c then cam_c.zoom = zoom end
end)

script_on_destroy(function()
    clock_update_remove(update_handle)
end)
