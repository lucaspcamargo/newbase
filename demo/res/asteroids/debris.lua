local LIFETIME = 1.0   -- seconds until fully faded

local timer = LIFETIME

local update_handle = clock_update_add(function(delta)
    timer = timer - delta
    if timer <= 0 then
        entity_destroy(eid)
        return
    end
    local spr_comp = c_sprite()
    if spr_comp then
        local c = spr_comp.color
        spr_comp.color = vec4.new(c.x, c.y, c.z, timer / LIFETIME)
    end
end)

script_on_destroy(function()
    clock_update_remove(update_handle)
end)
