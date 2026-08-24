require("colors")
require("draw_utils")

local FONT_CHR_W = 16
local fnt_blue = Sprites.find("fontlarge-blue")
local fnt_yellow = Sprites.find("fontlarge-yellow")


function title_draw(Frame)
    ui.cls(C_BLACK)
    draw_tile_box(176-32, 100-28, 12, 6)
    font_text_draw(fnt_blue, "PEQUENA", FONT_CHR_W, 176+8, 100)
    font_text_draw(fnt_yellow, "AVENTURA", FONT_CHR_W, 176, 100+24)
    if (Frame // 30) % 2 == 0 then
        local prompt = "APERTE Z PARA INICIAR"
        ui.print(prompt, 480/2 - ((#prompt)*6/2), 180, C_TEXT)
    end

    local copyright = "(C) CAMARGUINHO 2026 - ARTE POR SHARM"
    ui.print(copyright, 480/2 - ((#copyright)*6/2), 270-16, C_TEXT)
end