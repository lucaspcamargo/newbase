local FObj_Creature = require("fobj_creature")
local CharSheet = Sprites.find("characters")

local FObjNpc = setmetatable({}, FObj_Creature)
FObjNpc.__index = FObjNpc

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
	local obj = FObj_Creature.new(self, x, y, CharSheet, build_data.tile or 7, build_data)
	obj.dialogue = {"...?"}
	if build_data.dialogue and type(build_data.dialogue) == "string" then
        obj.dialogue = prepare_dialogue(build_data.dialogue)
    end
	return obj
end

function FObjNpc:is_solid()
	return true
end

function FObjNpc:is_interactive()
	return true
end

function FObjNpc:interact(field_controller)
	field_controller.dialogue(self.dialogue)
end

return FObjNpc
