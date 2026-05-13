local sfx_coin  = res_get_vorbis(hs("res/platformer/sfx/coin.ogg"))
local collected = false

local update_handle = clock_update_add(function(delta)
    if collected then return end

    local ph = sys_physics2d
    if not ph then return end

    local count = ph:contact_begins_count()
    for i = 0, count - 1 do
        local ea = ph:contact_begin_a(i)
        local eb = ph:contact_begin_b(i)
        if ea == eid or eb == eid then
            collected = true
            audio_sfx_play(hs("res/platformer/sfx/coin.ogg"), 1.0)
            if _G.PLAYER_COINS  then _G.PLAYER_COINS  = _G.PLAYER_COINS  + 1   end
            if _G.PLAYER_SCORE  then _G.PLAYER_SCORE  = _G.PLAYER_SCORE  + 100 end
            entity_destroy(eid)
            return
        end
    end
end)

script_on_destroy(function()
    clock_update_remove(update_handle)
end)
