
local box_spr = Sprites.find("basictiles")

local BOX_FRAME_TL = 93
local BOX_FRAME_TOP = 94
local BOX_FRAME_TR = 95

local BOX_FRAME_LEFT = 101
local BOX_FRAME_FILL = 102
local BOX_FRAME_RIGHT = 103

local BOX_FRAME_BL = 109
local BOX_FRAME_BOTTOM = 110
local BOX_FRAME_BR = 111

local BOX_TILE_W = 16
local BOX_TILE_H = 16

-- desenha uma caixa usando tiles do tileset basico
function draw_tile_box(x, y, tile_w, tile_h)
	if tile_w < 2 or tile_h < 2 then
		return
	end

	local right = x + (tile_w - 1) * BOX_TILE_W
	local bottom = y + (tile_h - 1) * BOX_TILE_H

	ui.tile(box_spr, BOX_FRAME_TL, x, y)
	ui.tile(box_spr, BOX_FRAME_TR, right, y)
	ui.tile(box_spr, BOX_FRAME_BL, x, bottom)
	ui.tile(box_spr, BOX_FRAME_BR, right, bottom)

	for column = 1, tile_w - 2 do
		local tile_x = x + column * BOX_TILE_W
		ui.tile(box_spr, BOX_FRAME_TOP, tile_x, y)
		ui.tile(box_spr, BOX_FRAME_BOTTOM, tile_x, bottom)
	end

	for row = 1, tile_h - 2 do
		local tile_y = y + row * BOX_TILE_H
		ui.tile(box_spr, BOX_FRAME_LEFT, x, tile_y)
		ui.tile(box_spr, BOX_FRAME_RIGHT, right, tile_y)

		for column = 1, tile_w - 2 do
			ui.tile(box_spr, BOX_FRAME_FILL,
				x + column * BOX_TILE_W, tile_y)
		end
	end
end


-- desenha texto utilizando um sprite como a fonte
-- usado para os fontes grandes do Tiny16
function largefont_text_draw(font, text, x, y, chr_width)
    local curr_x = x;
    local curr_y = y;
	chr_width = chr_width or 16

    for i = 1, #text do
        local char = text:sub(i, i)
        local char_idx

        if char >= "A" and char <= "Z" then
            char_idx = string.byte(char) - string.byte("A")
        elseif char >= "a" and char <= "z" then
            char_idx = 26 + string.byte(char) - string.byte("a")
        end

        if char_idx then
            ui.tile(font, char_idx, curr_x, curr_y)
        end

        curr_x = curr_x + chr_width
    end
end

local FONT_DEFAULT_NAME = "font_default"
local font_char_map = {}

-- desenha texto usando os indices da fonte gerada por make_font.py
function font_text_draw(fontname, text, x, y, chr_width, max_chars)
	fontname = fontname or FONT_DEFAULT_NAME
	font_spr = Sprites.find(fontname)
	font_char_map[fontname] = font_char_map[fontname] or require("gfx/"..fontname)
	chr_width = chr_width or font_char_map[fontname].char_w
	local curr_x = x

	local char_count = 0
	for _, codepoint in utf8.codes(text) do
		char_count = char_count + 1
		if max_chars and char_count > max_chars then
			break
		end
		local char = utf8.char(codepoint)
		local char_idx = font_char_map[fontname][char]
		if char_idx ~= nil then
			ui.tile(font_spr, char_idx, curr_x, y)
		end
		curr_x = curr_x + chr_width
		if font_char_map[fontname].outlined then
			curr_x = curr_x - 1
		end
	end
end

function font_get_char_width(fontname)
	fontname = fontname or FONT_DEFAULT_NAME
	font_char_map[fontname] = font_char_map[fontname] or require("gfx/"..fontname)
	return font_char_map[fontname].char_w - (font_char_map[fontname].outlined and 1 or 0)
end

