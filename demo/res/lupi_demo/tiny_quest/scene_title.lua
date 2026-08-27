local BaseScene = require("base_scene")
local SceneField = require("scene_field")
require("colors")
require("draw_utils")

local fnt_blue = Sprites.find("fontlarge-blue")
local fnt_yellow = Sprites.find("fontlarge-yellow")
local fnt = nil -- use default font

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
            self.next_scene = SceneField:new("tiled_test")
        end
    end
end

function SceneTitle:draw()
    ui.cls(C_BLACK)
    draw_tile_box(176-32, 80-28, 12, 6)
    largefont_text_draw(fnt_blue, "PEQUENA", 176+8, 80)
    largefont_text_draw(fnt_yellow, "AVENTURA", 176, 80+24)

    local font_char_w = font_get_char_width(fnt)
    if (self.frame // 30) % 2 == 0 then
        local prompt = "Aperte [Z] para começar"
        local prompt_x = 480/2 - ((#prompt)*font_char_w/2)
        font_text_draw(fnt, prompt, prompt_x, 170)
    end

    local copyright = "(C) Camarguinho 2026 - arte por Sharm"
    local copyright_x = 480/2 - ((#copyright)*font_char_w/2)
    font_text_draw(fnt, copyright, copyright_x, 270-24)
end

return SceneTitle