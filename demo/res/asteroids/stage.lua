-- stage script

physics2d_set_gravity(vec2.new(0.0, 0.0));

render_simple_cam_2d_setup(0, 0, 1920, 1080);

if sys_audio then
    audio_bgm_play(hs("res/asteroids/bgm/ObservingTheStar/ObservingTheStar.ogg"))
else
    print("[stage.lua] no audio service? skip bgm")
end