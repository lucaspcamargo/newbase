local FObj_Creature = require("fobj_creature")
local CharSheet = Sprites.find("characters")

local FObjNpc = setmetatable({}, FObj_Creature)
FObjNpc.__index = FObjNpc

local ROAM_INTERVAL = 48
local OPPOSITE_DIR = { d = "u", u = "d", l = "r", r = "l" }

local function prepare_dialogue(diag_str)
	local sep = "\n"
	local t = {}
	for str in string.gmatch(diag_str, "([^"..sep.."]+)") do
		table.insert(t, str)
	end
	return t
end

function FObjNpc:new(x, y, sprite, tile, build_data)
	build_data = build_data or {}
	build_data.skin = build_data.skin or "girl"
	local obj = FObj_Creature.new(self, x, y, CharSheet, build_data.tile or 7, build_data)
	obj.origin_x = obj.x
	obj.origin_y = obj.y
	obj.roam = tonumber(build_data.roam) or 0
	obj.roam_timer = 0
	obj.is_talking = false
	obj.dialogue = {"...?"}
	if build_data.dialogue and type(build_data.dialogue) == "string" then
        obj.dialogue = prepare_dialogue(build_data.dialogue)
    end
	return obj
end

function FObjNpc:try_roam(field_controller)
	if self.moving or self.is_talking or self.roam <= 0 then
		return
	end

	self.roam_timer = self.roam_timer + 1
	if self.roam_timer < ROAM_INTERVAL then
		return
	end

	self.roam_timer = 0
	local directions = { "d", "u", "l", "r" }
	for index = #directions, 2, -1 do
		local random_index = math.random(index)
		directions[index], directions[random_index] = directions[random_index], directions[index]
	end

	for _, direction in ipairs(directions) do
		local next_x, next_y = self.x, self.y
		if direction == "d" then next_y = next_y + 1
		elseif direction == "u" then next_y = next_y - 1
		elseif direction == "l" then next_x = next_x - 1
		else next_x = next_x + 1 end

		if math.abs(next_x - self.origin_x) + math.abs(next_y - self.origin_y) <= self.roam and
			self:try_start_move(field_controller, direction) then
			return
		end
	end
end

function FObjNpc:update(frame, field_controller)
	self:try_roam(field_controller)
	FObj_Creature.update(self, frame, field_controller)
end

function FObjNpc:is_interactive()
	return true
end

function FObjNpc:interact(field_controller, from_dir)
	self.is_talking = true
	self.dir = OPPOSITE_DIR[from_dir] or self.dir
	field_controller.dialogue(self.dialogue, self)
end

return FObjNpc
