local BaseFieldObj = require("base_fieldobj")

local FObj_Creature = setmetatable({}, BaseFieldObj)
FObj_Creature.__index = FObj_Creature

local SHEET_STRIDE = 12
local MOVE_FRAMES = 16
local DIRS = {
	d = { 0, 1 }, u = { 0, -1 }, l = { -1, 0 }, r = { 1, 0 },
}
local DIR_BUTTONS = {
	[LEFT] = "l", [RIGHT] = "r", [UP] = "u", [DOWN] = "d",
}
local FRAME_OFFSETS = {
	stand_d =   0,
	walk_a_d = -1,
	walk_b_d =  1,
	stand_l =   0 + SHEET_STRIDE,
	walk_a_l = -1 + SHEET_STRIDE,
	walk_b_l =  1 + SHEET_STRIDE,
	stand_r =   0 + SHEET_STRIDE * 2,
	walk_a_r = -1 + SHEET_STRIDE * 2,
	walk_b_r =  1 + SHEET_STRIDE * 2,
	stand_u =   0 + SHEET_STRIDE * 3,
	walk_a_u = -1 + SHEET_STRIDE * 3,
	walk_b_u =  1 + SHEET_STRIDE * 3,
}

local SKIN_BASE = {
	base   = 1,
	boy    = 4,
	girl   = 7,
	skelly = 10,
	slime  = 49,
	fly    = 52,
	ghost  = 55,
	spider = 58
}

-- get a frame index for a character skin
-- dir is either 'u', 'd', 'l', 'r'
-- frame_id is 'stand', 'walk_a', 'walk_b', and so on
local function creature_get_frame(skin, dir, frame_id)
	local skin_base = SKIN_BASE[skin]
	local frame_offset = FRAME_OFFSETS[frame_id .. "_" .. dir]
	if skin_base == nil then
		return 1
	end
	if frame_offset == nil then
		return skin_base
	end

	return skin_base + frame_offset
end

function FObj_Creature:new(x, y, sprite, tile, build_data)
	local obj = BaseFieldObj.new(self, x, y)
	build_data = build_data or {}
	obj.sprite = sprite
	obj.skin = build_data.skin or 'base'
	obj.dir = build_data.dir or 'd'
	obj.frame_id = build_data.frame_id or 'stand'
	obj.tile = tile or creature_get_frame(obj.skin, obj.dir, obj.frame_id)
	obj.from_x = obj.x
	obj.from_y = obj.y
	obj.px = obj.x * 16
	obj.py = obj.y * 16
	obj.moving = false
	obj.controllable = build_data.controllable == true
	obj.move_t = 0
	obj.anim_frame = 0
	obj.solid = true
	obj.build_data = build_data
	return obj
end

function FObj_Creature:try_start_move(field_controller, direction)
	if self.moving then return false end

	local dir = direction
	if not dir then
		for button, candidate in pairs(DIR_BUTTONS) do
			if ui.btn(button) then
				dir = candidate
				break
			end
		end
	end
	if not dir then return false end

	self.dir = dir
	local delta = DIRS[dir]
	local next_x, next_y = self.x + delta[1], self.y + delta[2]
	local fobj = field_controller.find_fobj(next_x, next_y)
	if field_controller.is_solid(next_x, next_y) or (fobj and fobj ~= self and fobj:is_solid()) then
		return false
	end

	self.moving = true
	self.move_t = 0
	self.anim_frame = 0
	self.from_x, self.from_y = self.x, self.y
	self.x, self.y = next_x, next_y
	return true
end

function FObj_Creature:update(frame, field_controller)
	if self.controllable and field_controller and field_controller:in_normal_mode() then
		self:try_start_move(field_controller)
	end

	if not self.moving then
		return
	end

	self.move_t = self.move_t + 1
	self.anim_frame = self.move_t // 4
	local progress = self.move_t / MOVE_FRAMES
	if progress >= 1 then
		progress = 1
		self.moving = false
		local stepped_on = field_controller.find_fobj(self.x, self.y, self) -- ignore self
		if stepped_on then
			stepped_on:on_creature_enter(self.controllable, field_controller)
		end
	end
	self.px = (self.from_x + (self.x - self.from_x) * progress) * 16
	self.py = (self.from_y + (self.y - self.from_y) * progress) * 16
end

function FObj_Creature:get_facing_position()
	local delta = DIRS[self.dir]
	return self.x + delta[1], self.y + delta[2]
end

function FObj_Creature:draw()
	local tile = self.tile
	if self.moving then
		local frame_ids = { 'walk_a', 'stand', 'walk_b', 'stand' }
		local frame_id = frame_ids[(self.anim_frame % #frame_ids) + 1]
		tile = creature_get_frame(self.skin, self.dir, frame_id)
	else
		tile = creature_get_frame(self.skin, self.dir, self.frame_id)
	end
	ui.tile(self.sprite, tile, math.floor(self.px), math.floor(self.py))
end

function FObj_Creature:is_solid()
	return self.solid
end

return FObj_Creature
