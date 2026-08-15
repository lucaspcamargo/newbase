-- newbase's own Lupi compatibility demo cart. Exercises cls/palset/rect(fill)/
-- circ(fill)/line/print/clip/camera/fillp/btn/spr so the "lupi framebuffer"
-- debug window (F10) has something to show, without depending on any
-- third-party cart.

local function rgb555(r, g, b) return (r << 10) | (g << 5) | b end

-- palette: 1 = sky, 2 = ground, 3 = accent, 4 = text
ui.palset(1, rgb555(10, 20, 31)) -- light blue
ui.palset(2, rgb555(4, 16, 4))   -- dark green
ui.palset(3, rgb555(31, 24, 0))  -- orange
ui.palset(4, rgb555(31, 31, 31)) -- white

local sprite = Sprites.find("newbase_sprite")
local sprite_x = 200

function update(frame)
    ui.cls(1) -- sky

    ui.clip(0, 0, 480, 270)
    ui.camera()

    ui.rectfill(0, 246, 479, 269, 2) -- ground

    -- clipped shape: centered on the clip boundary, so only its left half shows
    ui.clip(0, 0, 240, 270)
    ui.circfill(240, 80, 40, 3)
    ui.clip(0, 0, 480, 270)

    -- camera-shifted shape: drawn at (300,60) in world space but visibly
    -- offset on screen because of the camera
    ui.camera(20, 0)
    ui.rectfill(300, 60, 340, 100, 4)
    ui.camera()

    -- dithered vs solid circle, side by side
    ui.fillp(0b10101010, 0b01010101, 0b10101010, 0b01010101,
             0b10101010, 0b01010101, 0b10101010, 0b01010101)
    ui.circfill(380, 80, 20, 3)
    ui.fillp()
    ui.circfill(430, 80, 20, 3)

    ui.line(0, 230, 479, 230, 4)
    ui.print("NEWBASE LUPI DEMO", 8, 8, 4)

    if ui.btn(LEFT) then sprite_x = sprite_x - 2 end
    if ui.btn(RIGHT) then sprite_x = sprite_x + 2 end
    if sprite then ui.spr(sprite, sprite_x, 214) end

    ui.print(string.format("MEM %0.1f KB FPS %0.0f", ui.stat(0) / 1024, ui.stat(7)), 8, 258, 4)
end
