-- Tiny Quest — a small JRPG vertical slice for the Lupi console (and our compat system)
-- Art: Tiny 16: Basic by Lanea Zimmerman ("Sharm"), see gfx/LICENSE.txt.



require("colors")
require("title")
require("draw_utils")
require("map_utils")


local CharSheet = Sprites.find("characters")
local Overworld = map_prepare(require("maps.tiled_test"))

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

local function update_move()
    if not Player.moving then return end
    Player.move_t = Player.move_t + 1
    Player.anim_frame = Player.move_t // 6
    local t = Player.move_t / MOVE_FRAMES
    if t >= 1 then
        t = 1
        Player.moving = false
    end
    Player.px = (Player.from_col + (Player.col - Player.from_col) * t) * TILE
    Player.py = (Player.from_row + (Player.row - Player.from_row) * t) * TILE
end

local function start_dialogue()
    Dialogue.lines = {
        "Ola, viajante!",
        "Esta eh uma demo simples de JRPG.",
        "Vamos construir em cima disto!",
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
    draw_tile_box(8, 200, 29, 5 )
    ui.print(Dialogue.lines[Dialogue.idx], 33, 221, C_TEXT_SHADE)
    ui.print(Dialogue.lines[Dialogue.idx], 32, 220, C_TEXT)
    ui.print("Z: Continuar", 33, 245, C_TEXT_SHADE)
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
    end
end
