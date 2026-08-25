local BaseScene = require("base_scene")
local SceneField = require("scene_field")
require("colors")
require("draw_utils")


local FONT_CHR_W = 16
local fnt_blue = Sprites.find("fontlarge-blue")
local fnt_yellow = Sprites.find("fontlarge-yellow")

local SceneTitle = setmetatable({}, BaseScene)
SceneTitle.__index = SceneTitle

function SceneTitle:new()
    local scene = BaseScene.new(self)
    scene.frame = 0
    return scene
end

function SceneTitle:update(frame)
    self.frame = frame
    if ui.btnp(BTN_Z) then
            colors_fade_out()
            self.exit_timer = FADE_DEFAULT + 1
    end

    if self.exit_timer then
        self.exit_timer = self.exit_timer - 1
        if self.exit_timer == 0 then
            self.next_scene = SceneField:new()
        end
    end
end

function SceneTitle:draw()
    ui.cls(C_BLACK)
    draw_tile_box(176-32, 100-28, 12, 6)
    font_text_draw(fnt_blue, "PEQUENA", FONT_CHR_W, 176+8, 100)
    font_text_draw(fnt_yellow, "AVENTURA", FONT_CHR_W, 176, 100+24)
    if (self.frame // 30) % 2 == 0 then
        local prompt = "APERTE Z PARA INICIAR"
        ui.print(prompt, 480/2 - ((#prompt)*6/2), 180, C_TEXT)
    end

    local copyright = "(C) CAMARGUINHO 2026 - ARTE POR SHARM"
    ui.print(copyright, 480/2 - ((#copyright)*6/2), 270-16, C_TEXT)
end

return SceneTitle