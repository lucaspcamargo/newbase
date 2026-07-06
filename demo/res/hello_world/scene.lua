local _rs = svc_renderer_service()
if _rs then
    _rs:cam_2d_setup(0, 0, 1920, 1080)
    _rs:set_clear_color(0.3, 0.3, 0.3)
end

audio_bgm_play(hs("res/etc/navinhas loca.ogg"))