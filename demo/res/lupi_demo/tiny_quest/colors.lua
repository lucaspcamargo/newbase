-- Custom colors and Palette handling (fades and such)

-- Color handling is a bit confusing in Lupi right now.
-- From our findings:
-- * Palette[idx] is 1-based, always contains the original compiled palette
-- * ui.palset(idx, color) is 0-based, and sets the runtime Palette without affecting Palette[]. 
--
-- To maintain an index mapping with Palette, we need to apply an offset of -1 to the index. All in all, it is weird. Current implementation seems compatible with the existing games, though.

C_BATTLE_BG  = 248
C_FIELD_VOID = 249
C_TEXT       = 250
C_TEXT_FADED = 251
C_TEXT_SHADE = 252
C_HP_GOOD    = 253
C_HP_LOW     = 254
C_BLACK   = 255

FADE_DEFAULT = 30 -- standard fade-in/out duration

local function rgb555(r, g, b) return (r << 10) | (g << 5) | b end

local custom_colors = {
	[C_FIELD_VOID] = rgb555(2, 10, 2),
	[C_TEXT]       = rgb555(31, 31, 31),
	[C_TEXT_FADED] = rgb555(20, 20, 20),
	[C_TEXT_SHADE] = rgb555(2, 2, 2),
	[C_HP_GOOD]    = rgb555(4, 28, 4),
	[C_HP_LOW]     = rgb555(28, 4, 4),
	[C_BLACK]      = rgb555(1, 0, 0),
}

for index, color in pairs(custom_colors) do
	ui.palset(index, color)
end

local fade_frames = 0
local fade_frame = 0
local fade_in = true

local function fade_color(color, amount)
	local r = (color >> 10) & 31
	local g = (color >> 5) & 31
	local b = color & 31
	return rgb555(
		math.floor(r * amount + 0.5),
		math.floor(g * amount + 0.5),
		math.floor(b * amount + 0.5)
	)
end

function colors_fade_in(frames)
	frames = frames or FADE_DEFAULT
	fade_frames = math.max(0, frames or 0)
	fade_frame = 0
	fade_in = true
end

function colors_fade_out(frames)
	frames = frames or FADE_DEFAULT
	fade_frames = math.max(0, frames or 0)
	fade_frame = 0
	fade_in = false
end

function colors_update()
	if fade_frames == 0 then return end

	fade_frame = math.min(fade_frame + 1, fade_frames)
	local amount = fade_frame / fade_frames
	if not fade_in then amount = 1 - amount end

	for index = 1, 256 do
		local color = Palette[index]
		if color ~= nil then
			ui.palset(index-1, fade_color(color, amount))
		end
	end

    -- now, fade the custom colors on top
    for index, color in pairs(custom_colors) do
		ui.palset(index, fade_color(color, amount))
	end

	if fade_frame == fade_frames then
		fade_frames = 0
	end
end


