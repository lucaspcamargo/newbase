-- fixed-use colors

C_BATTLE_BG  = 248
C_FIELD_VOID = 249
C_TEXT       = 250
C_TEXT_FADED = 251
C_TEXT_SHADE = 252
C_HP_GOOD    = 253
C_HP_LOW     = 254
C_BLACK   = 255

local function rgb555(r, g, b) return (r << 10) | (g << 5) | b end

ui.palset(C_FIELD_VOID, rgb555(2, 10, 2))
ui.palset(C_TEXT,       rgb555(31, 31, 31))
ui.palset(C_TEXT_FADED, rgb555(20, 20, 20))
ui.palset(C_TEXT_SHADE, rgb555(2, 2, 2))
ui.palset(C_HP_GOOD,    rgb555(4, 28, 4))
ui.palset(C_HP_LOW,     rgb555(28, 4, 4))
ui.palset(C_BLACK,      rgb555(0, 0, 0))


