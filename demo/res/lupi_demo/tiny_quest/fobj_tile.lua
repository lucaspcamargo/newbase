local BaseFieldObj = require("base_fieldobj")

local FObjTile = setmetatable({}, BaseFieldObj)
FObjTile.__index = FObjTile

function FObjTile:new(x, y, sprite, tile)
	local obj = BaseFieldObj.new(self, x, y)
	obj.sprite = sprite
	obj.tile = tile
	return obj
end

function FObjTile:draw()
	ui.tile(self.sprite, self.tile, self.x*16, self.y*16)
end

function FObjTile:is_solid()
	return true
end

return FObjTile
