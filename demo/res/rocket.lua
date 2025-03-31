local eng = engine.ref()
print(eng)
print(cspatial)
print(spatial)
print(spatial.rot)
print(spatial.rot.z)
spatial.rot.z = 90
print(spatial.rot.z)
spatial:apply()

if system_audio then
    audio_bgm_play(hs("res/bgm/ObservingTheStar/ObservingTheStar.ogg"));
    audio_bgm_gain(0.5)
end
