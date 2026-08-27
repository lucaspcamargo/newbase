local BaseFieldObj = require("base_fieldobj")

local FObjPortal = setmetatable({}, BaseFieldObj)
FObjPortal.__index = FObjPortal

function FObjPortal:new(x, y, sprite, tile, build_data)
	local obj = BaseFieldObj.new(self, x, y)
	obj.sprite = sprite
	obj.tile = tile
    obj.target_map = build_data.map_id or "overworld"
    obj.target_wp = build_data.waypoint or "storage"
	return obj
end

function FObjPortal:update(frame)
end

function FObjPortal:draw()
	ui.tile(self.sprite, self.tile, self.x*16, self.y*16)
end

function FObjPortal:is_interactive()
	return true
end

function FObjPortal:interact(field_controller, from_dir)
	field_controller.dialogue({
		"Hmmm, isso vai pra " .. (self.target_map or "?") .. ": " .. (self.target_wp or "!?")
	})
end

function FObjPortal:is_solid()
	return false
end


function BaseFieldObj:on_creature_enter(is_player, field_controller)
	if is_player and self.target_map and self.target_wp then
        field_controller.map_change(self.target_map, self.target_wp)
    end
end

return FObjPortal
