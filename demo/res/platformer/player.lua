local dir_hs  = hs("dir")
local jump_hs = hs("btn_south")

local WALK_SPEED    = 350.0
local JUMP_VEL      = -620.0
local JUMP_CUT_VEL  = -250.0   -- upward speed clamped to on early release

local update_handle = clock_update_add(function(delta)
    local dir = input_action_direction(dir_hs)
    local ch  = c_character2d()
    local sp  = c_spatial()
    if not ch or not sp then return end

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

    -- Camera follow
    render_simple_cam_2d_setup(sp.pos.x, sp.pos.y, 1920, 1080)
end)

script_on_destroy(function()
    clock_update_remove(update_handle)
end)
