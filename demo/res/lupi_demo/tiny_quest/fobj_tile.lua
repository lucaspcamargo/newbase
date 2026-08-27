local BaseFieldObj = require("base_fieldobj")

local FObjTile = setmetatable({}, BaseFieldObj)
FObjTile.__index = FObjTile

function FObjTile:new(x, y, sprite, tile, build_data)
	local obj = BaseFieldObj.new(self, x, y)
	obj.sprite = sprite
	obj.tile = tile
    obj.solid = build_data.solid ~= false
    obj.interactive = build_data.dialogue or false
    obj.build_data = build_data
    obj.has_anim = build_data.anim and true
    obj.anim_data = {}
    obj.anim_frame = 0
    obj.anim_timer = 0
    if obj.has_anim then
        -- parse animation. a semicolon-separated list of tile_index:frame_duration pairs
        for entry in string.gmatch(build_data.anim, "[^;]+") do
            local tile_index, frame_duration = string.match(entry, "^%s*(%-?%d+)%s*:%s*(%d+)%s*$")
            tile_index = tonumber(tile_index)
            frame_duration = tonumber(frame_duration)
            if tile_index and frame_duration and frame_duration > 0 then
                table.insert(obj.anim_data, {
                    tile = tile_index,
                    duration = frame_duration,
                })
            end
        end
        obj.has_anim = #obj.anim_data > 0
    end
	return obj
end


function FObjTile:update(frame)
    if not self.has_anim then
        return
    end

    self.anim_timer = self.anim_timer + 1
    local current_frame = self.anim_data[self.anim_frame + 1]
    if self.anim_timer >= current_frame.duration then
        self.anim_timer = 0
        self.anim_frame = (self.anim_frame + 1) % #self.anim_data
    end
end

function FObjTile:is_interactive()
    return self.interactive
end

function FObjTile:interact(field_controller, from_dir)
    if not self.interactive then
        return
    end
	field_controller.dialogue({
		self.build_data.dialogue
	})
end

function FObjTile:draw()
    local tile = self.tile
    if self.has_anim then
        tile = self.anim_data[self.anim_frame + 1].tile
    end
    ui.tile(self.sprite, tile, self.x*16, self.y*16)
end

function FObjTile:is_solid()
	return self.solid
end

return FObjTile
