local BaseFieldObj = require("base_fieldobj")

local FObjWater = setmetatable({}, BaseFieldObj)
FObjWater.__index = FObjWater

local ANIM_SPEED = 12

function FObjWater:new(x, y, sprite, tile)
	local obj = BaseFieldObj.new(self, x, y)
	obj.sprite = sprite
	obj.tile = tile
	obj.alt_tile = tile+8
	obj.show_alt = false
	return obj
end

function FObjWater:update(frame)
	self.show_alt = ((frame + (self.x + self.y)*2)//12) % 2 == 1
end

function FObjWater:draw()
	ui.tile(self.sprite, self.show_alt and self.alt_tile or self.tile, self.x*16, self.y*16)
end



function FObjWater:is_interactive()
	return true
end

function FObjWater:interact(field_controller, from_dir)
	field_controller.dialogue({
		"Poxa, eu não sei nadar :("
	})
end

function FObjWater:is_solid()
	return true
end

return FObjWater
