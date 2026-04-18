-- Self-destructs after all particles have faded (lifetime + variance + margin)
local LIFETIME = 1.4

local timer = LIFETIME
local h = clock_update_add(function(delta)
    timer = timer - delta
    if timer <= 0 then
        entity_destroy(eid)
    end
end)

script_on_destroy(function()
    clock_update_remove(h)
end)
