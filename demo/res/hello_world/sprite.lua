-- cycle the tint of the sprite over time

local time = 0

local update_handle = clock_update_add(function(delta)
    time = time + delta*0.5
    local spr_comp = c_sprite()
    if spr_comp then
        local r = 0.5 + math.sin(time) * 0.25
        local g = 0.5 + math.sin(time + 2) * 0.25
        local b = 0.5 + math.sin(time + 4) * 0.25
        spr_comp.color = vec4.new(r, g, b, 1)
    end

    local sp = c_spatial()
    if sp then
        sp.scale = vec3.new(2 + math.sin(time)*0.25, 2 + math.sin(time)*0.25, 1)
        sp.rot = vec3.new(0, 0, time * 45)
        sp:apply()
    end
end)

script_on_destroy(function()
    clock_update_remove(update_handle)
end)