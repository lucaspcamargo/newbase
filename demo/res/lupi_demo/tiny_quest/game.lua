-- Tiny Quest — a small JRPG vertical slice for the "lupi" compat system.
-- Art: Tiny 16: Basic by Lanea Zimmerman ("Sharm"), see gfx/LICENSE.txt.
--
-- Flow: title -> overworld (walk + talk to the NPC) -> stepping into the
-- tall grass to the east can trigger a battle -> win/lose -> back to
-- overworld.

require("colors")
require("title")


local function rgb555(r, g, b) return (r << 10) | (g << 5) | b end

ui.palset(C_BATTLE_BG,  rgb555(10, 2, 2))
ui.palset(C_FIELD_VOID, rgb555(2, 10, 2))
ui.palset(C_TEXT,       rgb555(31, 31, 31))
ui.palset(C_BOX_BORDER, rgb555(4, 4, 6))
ui.palset(C_BOX_BG,     rgb555(6, 6, 10))
ui.palset(C_HP_GOOD,    rgb555(4, 28, 4))
ui.palset(C_HP_LOW,     rgb555(28, 4, 4))
ui.palset(C_TITLE_BG,   rgb555(4, 4, 14))

local CharSheet = Sprites.find("characters")
local Overworld = require("maps.tiled_test")

local TILE = 16
local MAP_W, MAP_H = Overworld.metadata.width, Overworld.metadata.height
local OFFSET_X = math.floor((480 - MAP_W * TILE) / 2)
local OFFSET_Y = math.floor((270 - MAP_H * TILE) / 2)

-- basictiles.png content-tile ids used by the map (see maps/overworld.json).
local WALKABLE = { [64] = true, [72] = true, [12] = true } -- grass, path, flowers

local function tile_id_at(col, row)
    if col < 0 or row < 0 or col >= MAP_W or row >= MAP_H then return nil end
    local raw = Overworld.base["basictiles"][row * MAP_W + col + 1]
    if not raw then return nil end
    return raw & 0x3FF
end

local function obj_tile_id_at(col, row)
    if col < 0 or row < 0 or col >= MAP_W or row >= MAP_H then return nil end
    local raw = Overworld.obj["basictiles"][row * MAP_W + col + 1]
    if not raw then return nil end
    return raw & 0x3FF
end

local function is_walkable(col, row)
    local id = tile_id_at(col, row)
    local obj_id = obj_tile_id_at(col, row)
    print("obj tile id at ", col, ", ", row, ": ", obj_id)
    return obj_id == nil -- and WALKABLE[id] == true
end

local function in_encounter_zone(col, row)
    return col >= 24 and col <= 27 and row >= 12 and row <= 14
end

-- characters.png content-tile ids (see gfx/LICENSE.txt for the sheet).
local WALK_FRAMES = {
    down  = { 3, 4, 5 },
    left  = { 15, 16, 17 },
    right = { 27, 28, 29 },
    up    = { 39, 40, 41 },
}
local DIRS = {
    down = { 0, 1 }, up = { 0, -1 }, left = { -1, 0 }, right = { 1, 0 },
}
local NPC_TILE = 7 -- second character's "down" idle frame
local ENEMY_TILE = 49 -- slime, idle frame

local NPC = { col = 20, row = 7 }

local Player = {
    col = 5, row = 9, from_col = 5, from_row = 9,
    px = 5 * TILE, py = 9 * TILE,
    dir = "down", moving = false, move_t = 0, anim_frame = 0,
}
local MOVE_FRAMES = 16

local Dialogue = { lines = {}, idx = 1 }
local Battle = {}

Frame = 0
Game = { state = "title" }

local function try_start_move()
    if Player.moving then return end
    local dir = nil
    if ui.btn(LEFT) then dir = "left"
    elseif ui.btn(RIGHT) then dir = "right"
    elseif ui.btn(UP) then dir = "up"
    elseif ui.btn(DOWN) then dir = "down" end
    if not dir then return end

    Player.dir = dir
    local d = DIRS[dir]
    local nc, nr = Player.col + d[1], Player.row + d[2]
    local walkable = is_walkable(nc, nr)
    local contains_npc = (nc == NPC.col and nr == NPC.row)
    if walkable and not contains_npc then
        Player.moving = true
        Player.move_t = 0
        Player.from_col, Player.from_row = Player.col, Player.row
        Player.col, Player.row = nc, nr
    else
        print("not_moving, walkable=", walkable, ", contains_npc=", contains_npc)
    end
end

local function start_battle()
    Battle.player_hp, Battle.player_hp_max = 20, 20
    Battle.enemy_hp, Battle.enemy_hp_max = 12, 12
    Battle.turn = "player"
    Battle.message = "Uma Meleca selvagem apareceu!"
    Battle.msg_timer = 60
    Game.state = "battle"
end

local function update_move()
    if not Player.moving then return end
    Player.move_t = Player.move_t + 1
    Player.anim_frame = Player.move_t // 6
    local t = Player.move_t / MOVE_FRAMES
    if t >= 1 then
        t = 1
        Player.moving = false
        if in_encounter_zone(Player.col, Player.row) and math.random(1, 8) == 1 then
            start_battle()
        end
    end
    Player.px = (Player.from_col + (Player.col - Player.from_col) * t) * TILE
    Player.py = (Player.from_row + (Player.row - Player.from_row) * t) * TILE
end

local function start_dialogue()
    Dialogue.lines = {
        "Ola, viajante!",
        "Cuidado com a grama alta cercada!",
        "Ha monstros escondidos...",
    }
    Dialogue.idx = 1
    Game.state = "dialogue"
end

local function try_interact()
    if Player.moving or not ui.btnp(BTN_Z) then return end
    local d = DIRS[Player.dir]
    local fc, fr = Player.col + d[1], Player.row + d[2]
    if fc == NPC.col and fr == NPC.row then
        start_dialogue()
    end
end

local function update_overworld()
    try_interact()
    try_start_move()
    update_move()
end

local function update_dialogue()
    if ui.btnp(BTN_Z) then
        Dialogue.idx = Dialogue.idx + 1
        if Dialogue.idx > #Dialogue.lines then
            Game.state = "overworld"
        end
    end
end

local function update_battle()
    if Battle.msg_timer > 0 then
        Battle.msg_timer = Battle.msg_timer - 1
        return
    end

    if Battle.turn == "player" then
        if ui.btnp(BTN_Z) then
            local dmg = math.random(3, 6)
            Battle.enemy_hp = math.max(0, Battle.enemy_hp - dmg)
            Battle.message = "Voce acertou e causou " .. dmg .. " de dano!"
            Battle.msg_timer = 40
            if Battle.enemy_hp <= 0 then
                Battle.message = "A Meleca foi derrotada!"
                Battle.msg_timer = 60
                Battle.turn = "win"
            else
                Battle.turn = "enemy"
                Battle.timer = 40
            end
        end
    elseif Battle.turn == "enemy" then
        Battle.timer = Battle.timer - 1
        if Battle.timer <= 0 then
            local dmg = math.random(2, 4)
            Battle.player_hp = math.max(0, Battle.player_hp - dmg)
            Battle.message = "Meleca te causou " .. dmg .. " de dano!"
            Battle.msg_timer = 40
            if Battle.player_hp <= 0 then
                Battle.message = "Voce foi derrotado..."
                Battle.msg_timer = 60
                Battle.turn = "lose"
            else
                Battle.turn = "player"
            end
        end
    elseif Battle.turn == "win" then
        if ui.btnp(BTN_Z) then Game.state = "overworld" end
    elseif Battle.turn == "lose" then
        if ui.btnp(BTN_Z) then
            Battle.player_hp = Battle.player_hp_max
            Game.state = "overworld"
        end
    end
end

local function draw_textbox(x0, y0, x1, y1)
    ui.rectfill(x0, y0, x1, y1, C_BOX_BG)
    ui.rect(x0, y0, x1, y1, C_BOX_BORDER)
end

local function draw_overworld()
    ui.cls(C_FIELD_VOID)
    ui.map(Overworld.base, OFFSET_X, OFFSET_Y)
    ui.map(Overworld.obj, OFFSET_X, OFFSET_Y)

    ui.tile(CharSheet, NPC_TILE, OFFSET_X + NPC.col * TILE, OFFSET_Y + NPC.row * TILE)

    local frames = WALK_FRAMES[Player.dir]
    local frame = Player.moving and frames[1 + (Player.anim_frame % 3)] or frames[2]
    ui.tile(CharSheet, frame, OFFSET_X + math.floor(Player.px), OFFSET_Y + math.floor(Player.py))

    ui.map(Overworld.cover, OFFSET_X, OFFSET_Y)
end

local function draw_dialogue()
    draw_overworld()
    draw_textbox(20, 205, 460, 260)
    ui.print(Dialogue.lines[Dialogue.idx], 32, 220, C_TEXT)
    ui.print("Z: Continuar", 32, 244, C_TEXT)
end

local function draw_hpbar(x, y, hp, hp_max)
    local w = 80
    ui.rectfill(x, y, x + w, y + 8, C_BOX_BORDER)
    local fillw = math.floor(w * hp / hp_max)
    if fillw > 0 then
        ui.rectfill(x, y, x + fillw, y + 8, hp / hp_max > 0.3 and C_HP_GOOD or C_HP_LOW)
    end
end

local function draw_battle()
    ui.cls(C_BATTLE_BG)
    ui.tile(CharSheet, ENEMY_TILE, 220, 60)

    ui.print("VOCE", 40, 190, C_TEXT)
    draw_hpbar(40, 202, Battle.player_hp, Battle.player_hp_max)
    ui.print("MELECA", 360, 30, C_TEXT)
    draw_hpbar(360, 42, Battle.enemy_hp, Battle.enemy_hp_max)

    draw_textbox(20, 225, 460, 260)
    ui.print(Battle.message, 32, 232, C_TEXT)
    if Battle.msg_timer <= 0 then
        if Battle.turn == "player" then
            ui.print("Z: Atacar", 32, 246, C_HP_GOOD)
        elseif Battle.turn == "win" or Battle.turn == "lose" then
            ui.print("Z: Continuar", 32, 246, C_HP_GOOD)
        end
    end
end




function update(frame)
    Frame = Frame + 1

    if Game.state == "title" then
        title_draw(Frame)
        if ui.btnp(BTN_Z) then Game.state = "overworld" end
    elseif Game.state == "overworld" then
        update_overworld()
        draw_overworld()
    elseif Game.state == "dialogue" then
        update_dialogue()
        draw_dialogue()
    elseif Game.state == "battle" then
        update_battle()
        draw_battle()
    end
end
