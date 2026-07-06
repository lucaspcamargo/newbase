local HUD_SPRITE_ETREE = hs("res/platformer/hud_sprite.et.yaml")
local _res_hud_sprite  = res_get_etree(HUD_SPRITE_ETREE)

local _rs = svc_renderer_service()
local HUD_SCALE = _rs and _rs:display_scale() or 1.0

-- Sprite half-widths and half-heights at 1x (anchor is center = 0.5,0.5)
-- Widths from hud.sprite source_rect[3], heights from source_rect[4]
local S = HUD_SCALE
local HW = { -- half-widths
    p1=23*S, x=15*S, coin=23*S, heart=26*S,
    ["0"]=15*S,["1"]=13*S,["2"]=16*S,["3"]=14*S,["4"]=14*S,
    ["5"]=14*S,["6"]=15*S,["7"]=16*S,["8"]=16*S,["9"]=16*S,
}
local HH = { -- half-heights
    p1=23*S, x=14*S, coin=23*S, heart=22*S,
    digit=19*S,
}
local DIGIT_SLOT = 17*S  -- half-slot for each digit (fixed width for all digits)
local ELEM_GAP   =  4*S
local GROUP_GAP  = 22*S
local MARGIN     = 18*S

-- Row centers (y)
local ROW1_CY = MARGIN + HH.p1              -- top of screen, centered on tallest (p1=47px)
local ROW2_CY = ROW1_CY + HH.p1 + 10*S + HH.coin  -- second row

local function spawn(seq)
    local e = entity_spawn(HUD_SPRITE_ETREE)
    if e then
        local s = get_sprite(e)
        if s then s.sequence = seq end
        local sp = get_spatial(e)
        if sp then sp.scale = vec3.new(HUD_SCALE, HUD_SCALE, 1) end
    end
    return e
end

local function set_pos(e, cx, cy)
    local sp = get_spatial(e)
    if sp then
        sp.pos = vec3.new(cx, cy, 0)
        sp:apply()
    end
end

local function set_seq(e, seq)
    local s = get_sprite(e)
    if s then s.sequence = seq end
end

local function digit_seq(d)
    return "hud_" .. tostring(d % 10)
end

-- Spawn HUD elements
local e_lives_icon  = spawn("hud_p1")
local e_lives_x     = spawn("hud_x")
local e_lives_digit = spawn("hud_0")

local MAX_HP = 3
local e_hearts = {}
for i = 1, MAX_HP do e_hearts[i] = spawn("hud_heartFull") end

local e_coin_icon   = spawn("hud_coins")
local e_coin_x      = spawn("hud_x")
local COIN_DIGITS   = 3
local e_coin_digits = {}
for i = 1, COIN_DIGITS do e_coin_digits[i] = spawn("hud_0") end

local SCORE_DIGITS   = 9
local e_score_digits = {}
for i = 1, SCORE_DIGITS do e_score_digits[i] = spawn("hud_0") end

local function update_layout()
    -- Row 1: [p1][x][lives]  [heart][heart][heart]
    local x = MARGIN + HW.p1
    set_pos(e_lives_icon, x, ROW1_CY)
    x = x + HW.p1 + ELEM_GAP + HW.x

    set_pos(e_lives_x, x, ROW1_CY)
    x = x + HW.x + ELEM_GAP + DIGIT_SLOT

    local lives = math.max(0, math.min(9, _G.PLAYER_LIVES or 3))
    set_seq(e_lives_digit, digit_seq(lives))
    set_pos(e_lives_digit, x, ROW1_CY)
    x = x + DIGIT_SLOT + GROUP_GAP + HW.heart

    local hp = math.max(0, math.min(MAX_HP, _G.PLAYER_HP or MAX_HP))
    for i = 1, MAX_HP do
        set_seq(e_hearts[i], hp >= i and "hud_heartFull" or "hud_heartEmpty")
        set_pos(e_hearts[i], x, ROW1_CY)
        x = x + HW.heart + (i < MAX_HP and ELEM_GAP + HW.heart or 0)
    end

    -- Row 2: [coin][x][NNN]  [NNNNNNNNN]
    x = MARGIN + HW.coin
    set_pos(e_coin_icon, x, ROW2_CY)
    x = x + HW.coin + ELEM_GAP + HW.x

    set_pos(e_coin_x, x, ROW2_CY)
    x = x + HW.x + ELEM_GAP + DIGIT_SLOT

    local coins = math.max(0, math.min(10^COIN_DIGITS - 1, _G.PLAYER_COINS or 0))
    for i = 1, COIN_DIGITS do
        local d = math.floor(coins / 10^(COIN_DIGITS - i)) % 10
        set_seq(e_coin_digits[i], digit_seq(d))
        set_pos(e_coin_digits[i], x, ROW2_CY)
        x = x + DIGIT_SLOT * 2
    end
    x = x + GROUP_GAP + DIGIT_SLOT

    local score = math.max(0, math.min(10^SCORE_DIGITS - 1, _G.PLAYER_SCORE or 0))
    local leading = true
    for i = 1, SCORE_DIGITS do
        local d = math.floor(score / 10^(SCORE_DIGITS - i)) % 10
        if d ~= 0 then leading = false end
        local is_leading = leading and (i < SCORE_DIGITS)  -- last digit always full opacity
        set_seq(e_score_digits[i], digit_seq(d))
        set_pos(e_score_digits[i], x, ROW2_CY)
        local spr = get_sprite(e_score_digits[i])
        if spr then spr.color = vec4.new(1, 1, 1, is_leading and 0.3 or 1.0) end
        x = x + DIGIT_SLOT * 2
    end
end

local pause_hs = hs("start")
local paused   = false

local update_handle = clock_update_add(function()
    -- keep HUD camera centered on the window
    if _G.HUD_CAMERA_EID then
        local hsp = get_spatial(_G.HUD_CAMERA_EID)
        if hsp then
            local _rs2 = svc_renderer_service()
            hsp.pos = vec3.new(_rs2 and _rs2:window_width()  * 0.5 or 0,
                               _rs2 and _rs2:window_height() * 0.5 or 0, 0)
            hsp:apply()
        end
    end

    update_layout()

    if input_action_was_pressed(pause_hs) then
        paused = not paused
        clock_set_time_scale(paused and 0.0 or 1.0)
    end
end)

-- set positions before the first update runs
update_layout()

script_on_destroy(function()
    clock_update_remove(update_handle)
    for _, e in ipairs(e_hearts)      do entity_destroy(e) end
    for _, e in ipairs(e_coin_digits)  do entity_destroy(e) end
    for _, e in ipairs(e_score_digits) do entity_destroy(e) end
    entity_destroy(e_lives_icon)
    entity_destroy(e_lives_x)
    entity_destroy(e_lives_digit)
    entity_destroy(e_coin_icon)
    entity_destroy(e_coin_x)
    if paused then clock_set_time_scale(1.0) end
end)
