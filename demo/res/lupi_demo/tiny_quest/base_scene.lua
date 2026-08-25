-- base object for a game scene
-- used by the main game controller

require("colors")

local BaseScene = {}
BaseScene.__index = BaseScene

function BaseScene:new()
	return setmetatable({
		next_scene = nil
	}, self)
end

function BaseScene:update(frame)
end

function BaseScene:draw()
    ui.cls(C_BLACK)
	local msg = "(CENA BASE, VAZIA)"
    ui.print(msg, 480/2 - ((#msg)*6/2), 270/2-8, C_TEXT)
end

function BaseScene:get_next_scene()
	return self.next_scene
end

return BaseScene
