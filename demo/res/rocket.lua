local eng = engine.ref()

if system_audio then
    audio_bgm_play(hs("res/bgm/ObservingTheStar/ObservingTheStar.ogg"));
    audio_bgm_gain(0.5)
end

local action_id = hs("btn_south")
local dir_hs = hs("dir")

clock_update_add(function (delta)
    local sp = spatial()
    local dir = input_action_direction(dir_hs)
    sp.pos = sp.pos + dir*delta*500.0
    if input_action_is_pressed(action_id) then
        sp.rot.z = sp.rot.z + 50*delta
    else
        sp.rot.z = sp.rot.z - 50*delta
    end
    sp:apply()
end)