local BaseFieldObj = require("base_fieldobj")

local FObj_Creature = setmetatable({}, BaseFieldObj)
FObj_Creature.__index = FObj_Creature

function FObj_Creature:new(x, y, sprite, tile, build_data)
	local obj = BaseFieldObj.new(self, x, y)
	build_data = build_data or {}
	obj.sprite = sprite
	obj.tile = tile or build_data.tile or 0
	obj.build_data = build_data
	return obj
end

function FObj_Creature:draw()
	ui.tile(self.sprite, self.tile, self.x * 16, self.y * 16)
end

return FObj_Creature
