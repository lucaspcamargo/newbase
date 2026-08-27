local BaseFieldObj = require("base_fieldobj")

local FObjDoor = setmetatable({}, BaseFieldObj)
FObjDoor.__index = FObjDoor

function FObjDoor:new(x, y, sprite, tile, build_data)
	local obj = BaseFieldObj.new(self, x, y)
	obj.sprite = sprite
	obj.tile = tile
    obj.solid = build_data.solid ~= false
    obj.interactive = build_data.dialogue or false
    obj.build_data = build_data
	return obj
end


function FObjDoor:update(frame)
    return
end

function FObjDoor:is_interactive()
    return self.interactive
end

function FObjDoor:interact(field_controller, from_dir)
    if not self.interactive then
        return
    end
	field_controller.dialogue({
		self.build_data.dialogue
	})
end

function FObjDoor:draw()
    ui.tile(self.sprite, self.tile, self.x*16, self.y*16)
end

function FObjDoor:is_solid()
	return self.solid
end

return FObjDoor
