local BaseScene = require("base_scene")
require("colors")
require("draw_utils")
require("map_utils")

local FObj_Creature = require("fobj_creature")

local BTN_DEBUG = BTN_F
local TILE_SZ = 16
local TRANSITION_DURATION = 40

local SceneField = setmetatable({}, BaseScene)
SceneField.__index = SceneField

function SceneField:new(field_id)
    local scene = BaseScene.new(self)
    scene.field_id = field_id
    scene.map = map_load(scene.field_id)
    scene.map_w = scene.map.metadata.width
    scene.map_h = scene.map.metadata.height
    scene.cam_x = (480 - scene.map_w * TILE_SZ) // 2
    scene.cam_y = (270 - scene.map_h * TILE_SZ) // 2
    scene.frame = 0
    scene.mode = "transition_in"
    scene.debug = false
    scene.player = FObj_Creature:new(5, 9, Sprites.find("characters"), nil, {
        skin = "boy",
        controllable = true,
    })
    scene.dialogue = { lines = {}, idx = 1 }
    scene.fobjs = scene.map.fobjs
    table.insert(scene.fobjs, scene.player)
    scene.field_controller = {
        find_fobj = function (x, y)
            return scene:find_fobj(x, y)
        end,
        is_solid = function (x, y)
            return map_is_solid(scene.map, x, y)
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
    scene.first_update = true
    scene.transition_out_start = 0
    scene.transition_out_data = {}
    return scene
end

function SceneField:update(frame)
    self.frame = frame

    if self.first_update then
        colors_reset()
        self.first_update = false
    end

    if self.mode == "transition_in" and frame == TRANSITION_DURATION then
        self.mode = "map" -- end in transition
    end

    if self.mode == "map" then
        self:update_map()
    elseif self.mode == "dialogue" then
        self:update_dialogue()
    end

    if self.mode == "transition_out" and (self.transition_out_start + TRANSITION_DURATION) == frame then 
        print("CHANGE MAP "..str(self.transition_out_data))        
    end

    for _, fobj in ipairs(self.fobjs) do
        fobj:update(frame, self.field_controller)
    end

    if ui.btnp(BTN_DEBUG) then
        self.debug = not self.debug
    end

    if self.debug and ui.btn(BTN_DEBUG) then
        if ui.btn(LEFT) then
            self.cam_x =self.cam_x - 1
        end
        if ui.btn(RIGHT) then
            self.cam_x =self.cam_x + 1
        end
        if ui.btn(UP) then
            self.cam_y =self.cam_y - 1
        end
        if ui.btn(DOWN) then
            self.cam_y =self.cam_y + 1
        end
    end 



    if not ui.btn(BTN_DEBUG) then
        --update cam
        local player = self.player

        local new_cam_x = -player.px + 480//2 - TILE_SZ//2
        local new_cam_y = -player.py + 270//2 - TILE_SZ//2
        
        -- TODO bounds check with self.map_[wh]*TILE_SZ
        new_cam_x = math.max(math.min(0, new_cam_x), 480 - self.map_w*TILE_SZ)
        new_cam_y = math.max(math.min(0, new_cam_y), 270 - self.map_h*TILE_SZ)
        

        self.cam_x = new_cam_x
        self.cam_y = new_cam_y
    end
end

function SceneField:draw()
    self:draw_map()

    if self.mode == "dialogue" then
        self:draw_dialogue()
    end

    if self.mode == "transition_in" then
        -- draw in transition
        for x=0, 30 do
            for y=0, 17 do
                local delay = (x + y)//2
                x0 = x*16
                y0 = y*16
                x1 = x*16 + math.min(16, 16 - self.frame + delay)
                y1 = y*16 + math.min(16, 16 - self.frame + delay)
                if x1 >= x0 and y1 >= y0 then
                    ui.rectfill(x0, y0, x1, y1, C_BLACK)
                end
            end
        end
    end

    if self.mode == "transition_out" then
        -- draw out transition
        local timer = self.frame - self.transition_out_start
        for x=0, 30 do
            for y=0, 17 do
                local delay = (30 + 17 - x - y)//2
                x0 = x*16
                y0 = y*16
                x1 = x*16 + math.min(16, timer - delay)
                y1 = y*16 + math.min(16, timer - delay)
                if x1 >= x0 and y1 >= y0 then
                    ui.rectfill(x0, y0, x1, y1, C_BLACK)
                end
            end
        end
    end

    if self.debug then
        self:draw_debug()
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

function SceneField:update_map()
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
        self.mode = "map"
    end
end

function SceneField:draw_map()
    local player = self.player
    ui.cls(C_FIELD_VOID)

    ui.camera(-self.cam_x, -self.cam_y)

    ui.map(self.map.base, 0, 0)
    ui.map(self.map.base_decor, 0, 0)

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

    ui.map(self.map.cover, 0, 0)

    -- reset camera
    ui.camera(0,0)
end

function SceneField:draw_dialogue()

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
    ui.print("CAM_X: " .. self.cam_x .. "  CAM_Y: " .. self.cam_y, 8, 56, C_TEXT)
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