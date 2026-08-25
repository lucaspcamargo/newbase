local BaseScene = require("base_scene")
require("colors")
require("draw_utils")
require("map_utils")

local CharSheet = Sprites.find("characters")

local TILE_SZ = 16

local WALK_FRAMES = {
    down  = { 3, 4, 5 },
    left  = { 15, 16, 17 },
    right = { 27, 28, 29 },
    up    = { 39, 40, 41 },
}
local DIRS = {
    down = { 0, 1 }, up = { 0, -1 }, left = { -1, 0 }, right = { 1, 0 },
}
local NPC_TILE = 7
local NPC = { col = 20, row = 7 }
local MOVE_FRAMES = 16

local SceneField = setmetatable({}, BaseScene)
SceneField.__index = SceneField

function SceneField:new()
    local scene = BaseScene.new(self)
    scene.overworld = map_prepare(require("maps.tiled_test"))
    scene.map_w = scene.overworld.metadata.width
    scene.map_h = scene.overworld.metadata.height
    scene.offset_x = math.floor((480 - scene.map_w * TILE_SZ) / 2)
    scene.offset_y = math.floor((270 - scene.map_h * TILE_SZ) / 2)
    scene.frame = 0
    scene.mode = "overworld"
    scene.debug = false
    scene.player = {
        col = 5, row = 9, from_col = 5, from_row = 9,
        px = 5 * TILE_SZ, py = 9 * TILE_SZ,
        dir = "down", moving = false, move_t = 0, anim_frame = 0,
    }
    scene.dialogue = { lines = {}, idx = 1 }
    scene.fobjs = scene.overworld.fobjs
    scene.field_controller = {
        dialogue = function (lines)
            scene.dialogue = { lines = lines, idx = 1 }
            scene.mode = "dialogue"
        end
    }
    return scene
end

function SceneField:update(frame)
    self.frame = frame

    if self.mode == "overworld" then
        self:update_overworld()
    elseif self.mode == "dialogue" then
        self:update_dialogue()
    end

    -- update field objects
    for _, fobj in ipairs(self.fobjs) do
        fobj:update(frame)
    end

    if ui.btnp(BTN_X) then
        self.debug = not self.debug
    end
end

function SceneField:draw()
    if self.mode == "overworld" then
        self:draw_overworld()
    elseif self.mode == "dialogue" then
        self:draw_dialogue()
    end

    if self.debug then
        self:draw_debug()
    end
end

function SceneField:try_start_move()
    local player = self.player
    if player.moving then return end

    local dir = nil
    if ui.btn(LEFT) then dir = "left"
    elseif ui.btn(RIGHT) then dir = "right"
    elseif ui.btn(UP) then dir = "up"
    elseif ui.btn(DOWN) then dir = "down" end
    if not dir then return end

    player.dir = dir
    local delta = DIRS[dir]
    local next_col, next_row = player.col + delta[1], player.row + delta[2]
    local walkable = not map_is_solid(self.overworld, next_col, next_row)
    local contains_npc = next_col == NPC.col and next_row == NPC.row
    if walkable and not contains_npc then
        player.moving = true
        player.move_t = 0
        player.from_col, player.from_row = player.col, player.row
        player.col, player.row = next_col, next_row
    end
end

function SceneField:update_move()
    local player = self.player
    if not player.moving then return end

    player.move_t = player.move_t + 1
    player.anim_frame = player.move_t // 6
    local progress = player.move_t / MOVE_FRAMES
    if progress >= 1 then
        progress = 1
        player.moving = false
    end
    player.px = (player.from_col + (player.col - player.from_col) * progress) * TILE_SZ
    player.py = (player.from_row + (player.row - player.from_row) * progress) * TILE_SZ
end

function SceneField:start_dialogue()
    self.dialogue.lines = {
        "Ola, viajante!",
        "Esta eh uma demo simples de JRPG.",
        "Vamos construir em cima disto!",
    }
    self.dialogue.idx = 1
    self.mode = "dialogue"
end

function SceneField:try_interact()
    local player = self.player
    if player.moving or not ui.btnp(BTN_Z) then return end

    local delta = DIRS[player.dir]
    local facing_col, facing_row = player.col + delta[1], player.row + delta[2]
    if facing_col == NPC.col and facing_row == NPC.row then
        self:start_dialogue()
        return
    end

    fobj_here = self:find_fobj(facing_col, facing_row)
    if fobj_here and fobj_here:is_interactive() then
        fobj_here:interact(self.field_controller)
    end

end

function SceneField:update_overworld()
    self:try_interact()
    self:try_start_move()
    self:update_move()
end

function SceneField:update_dialogue()
    if not ui.btnp(BTN_Z) then return end

    self.dialogue.idx = self.dialogue.idx + 1
    if self.dialogue.idx > #self.dialogue.lines then
        self.mode = "overworld"
    end
end

function SceneField:draw_overworld()
    local player = self.player
    ui.cls(C_FIELD_VOID)
    ui.map(self.overworld.base, self.offset_x, self.offset_y)
    ui.map(self.overworld.base_decor, self.offset_x, self.offset_y)

    -- now, draw field objects, sorted by y coord, then x coord
    local sorted_fobjs = {}
    for _, fobj in ipairs(self.fobjs) do
        table.insert(sorted_fobjs, fobj)
    end
    table.sort(sorted_fobjs, function(first, second)
        if first.y == second.y then
            return first.x < second.x
        end
        return first.y < second.y
    end)
    for _, fobj in ipairs(sorted_fobjs) do
        fobj:draw()
    end

    ui.tile(CharSheet, NPC_TILE, self.offset_x + NPC.col * TILE_SZ, self.offset_y + NPC.row * TILE_SZ)

    local frames = WALK_FRAMES[player.dir]
    local frame = player.moving and frames[1 + (player.anim_frame % 3)] or frames[2]
    ui.tile(CharSheet, frame, self.offset_x + math.floor(player.px), self.offset_y + math.floor(player.py))

    ui.map(self.overworld.cover, self.offset_x, self.offset_y)
end

function SceneField:draw_dialogue()
    self:draw_overworld()

    local line = self.dialogue.lines[self.dialogue.idx]
    if line == nil then return end

    draw_tile_box(8, 200, 29, 5)
    ui.print(line, 33, 221, C_TEXT_SHADE)
    ui.print(line, 32, 220, C_TEXT)
    ui.print("Z: Continuar", 33, 245, C_TEXT_SHADE)
    ui.print("Z: Continuar", 32, 244, C_TEXT)
end

function SceneField:draw_debug()
    local player = self.player
    ui.print("X: " .. player.col .. "  Y: " .. player.row, 8, 8, C_TEXT)
    ui.print("PX: " .. math.floor(player.px) .. "  PY: " .. math.floor(player.py), 8, 20, C_TEXT)
    ui.print("DIR: " .. player.dir .. "  MOVING: " .. tostring(player.moving), 8, 32, C_TEXT)
    ui.print("MODE: " .. self.mode .. "  FRAME: " .. self.frame, 8, 44, C_TEXT)
end

function SceneField:find_fobj(x, y)
    -- try to find a field object at specified coordinates
    -- returns nil when there's nothing
    for _, fobj in ipairs(self.fobjs) do
        if fobj.x == x and fobj.y == y then
            return fobj
        end
    end

    return nil
end


return SceneField