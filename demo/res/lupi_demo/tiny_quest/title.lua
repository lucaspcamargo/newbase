require("colors")

function title_draw(Frame)
    ui.cls(C_TITLE_BG)
    ui.print("PEQUENA AVENTURA", 200, 110, C_TEXT)
    if (Frame // 30) % 2 == 0 then
        ui.print("APERTE Z PARA INICIAR", 165, 140, C_TEXT)
    end
end