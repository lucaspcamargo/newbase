-- stage script

physics2d_set_gravity(vec2.new(0.0, 0.0));
render_cam_2d_setup(0, 0, 1024, 1024);

if system_audio then
    audio_bgm_play(hs("res/asteroids/bgm/ObservingTheStar/ObservingTheStar.ogg"));
    audio_bgm_gain(0.5)
end