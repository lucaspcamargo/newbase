local BaseFieldObj = {}
BaseFieldObj.__index = BaseFieldObj

function BaseFieldObj:new(x, y)
	return setmetatable({
		x = x or 0,
		y = y or 0,
	}, self)
end

function BaseFieldObj:update(frame, field_controller)
end

function BaseFieldObj:draw()
end

function BaseFieldObj:is_solid()
	return false
end

function BaseFieldObj:is_interactive()
	return false
end

function BaseFieldObj:interact(field_controller, from_dir)
end

return BaseFieldObj
