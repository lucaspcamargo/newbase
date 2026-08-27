local BaseScene = require("base_scene")
require("colors")
require("draw_utils")
require("map_utils")
local FObj_Creature = require("fobj_creature")

local TILE_SZ = 16

local SceneField = setmetatable({}, BaseScene)
SceneField.__index = SceneField

function SceneField:new()
    local scene = BaseScene.new(self)
    scene.overworld = map_prepare(require("maps.tiled_test"), "./maps/tiled_test.json")
    scene.map_w = scene.overworld.metadata.width
    scene.map_h = scene.overworld.metadata.height
    scene.offset_x = math.floor((480 - scene.map_w * TILE_SZ) / 2)
    scene.offset_y = math.floor((270 - scene.map_h * TILE_SZ) / 2)
    scene.frame = 0
    scene.mode = "overworld"
    scene.debug = false
    scene.player = FObj_Creature:new(5, 9, Sprites.find("characters"), nil, {
        skin = "boy",
        controllable = true,
    })
    scene.dialogue = { lines = {}, idx = 1 }
    scene.fobjs = scene.overworld.fobjs
    table.insert(scene.fobjs, scene.player)
    scene.field_controller = {
        find_fobj = function (x, y)
            return scene:find_fobj(x, y)
        end,
        is_solid = function (x, y)
            return map_is_solid(scene.overworld, x, y)
        end,
        dialogue = function (lines, speaker)
            scene.dialogue = { lines = lines, idx = 1 }
            scene.dialogue.visible_chars = 0
            scene.dialogue.reveal_timer = 0
            scene.active_talker = speaker
            scene.mode = "dialogue"
        end,
        in_dialogue = function()
            return scene.mode == "dialogue"
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

    for _, fobj in ipairs(self.fobjs) do
        fobj:update(frame, self.field_controller)
    end
end

function SceneField:draw()
    if self.mode == "overworld" then
        self:draw_overworld()
    elseif self.mode == "dialogue" then
        self:draw_dialogue()
    end
end

function SceneField:try_interact()
    local player = self.player
    if player.moving or not ui.btnp(BTN_Z) then return end

    local facing_col, facing_row = player:get_facing_position()
    local fobj_here = self:find_fobj(facing_col, facing_row)
    if fobj_here and fobj_here:is_interactive() then
        fobj_here:interact(self.field_controller, player.dir)
    end

end

function SceneField:update_overworld()
    self:try_interact()
end

function SceneField:update_dialogue()
    local line = self.dialogue.lines[self.dialogue.idx]
    local line_length = utf8.len(line or "")
    if self.dialogue.visible_chars < line_length then
        self.dialogue.reveal_timer = self.dialogue.reveal_timer + 1
        if self.dialogue.reveal_timer >= 2 then
            self.dialogue.visible_chars = math.min(self.dialogue.visible_chars + 1, line_length)
            self.dialogue.reveal_timer = 0
        end

        if ui.btnp(BTN_Z) then
            self.dialogue.visible_chars = line_length
            self.dialogue.reveal_timer = 0
        end
        return
    end

    if not ui.btnp(BTN_Z) then return end

    self.dialogue.idx = self.dialogue.idx + 1
    self.dialogue.visible_chars = 0
    self.dialogue.reveal_timer = 0
    if self.dialogue.idx > #self.dialogue.lines then
        if self.active_talker then
            self.active_talker.is_talking = false
            self.active_talker = nil
        end
        self.mode = "overworld"
    end
end

function SceneField:draw_overworld()
    local player = self.player
    ui.cls(C_FIELD_VOID)

    ui.camera(-self.offset_x, -self.offset_y)

    ui.map(self.overworld.base, 0, 0)
    ui.map(self.overworld.base_decor, 0, 0)

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

    ui.map(self.overworld.cover, 0, 0)

    -- reset camera
    ui.camera(0,0)
end

function SceneField:draw_dialogue()
    self:draw_overworld()

    local line = self.dialogue.lines[self.dialogue.idx]
    if line == nil then return end

    draw_tile_box(8, 200, 29, 5)
    font_text_draw(nil, line, 32, 220, nil, self.dialogue.visible_chars)
    font_text_draw(nil, "Z: Continuar", 32, 244)
end

function SceneField:draw_debug()
    local player = self.player
    ui.print("X: " .. player.x .. "  Y: " .. player.y, 8, 8, C_TEXT)
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