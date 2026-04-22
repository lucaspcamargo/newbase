#pragma once

#include "newbase/system.hpp"

namespace nb {

struct audio_p;

class audio : public system {
public:
    audio();
    ~audio() override;

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef) override;
    entt::id_type metatype_id() override { return entt::hashed_string{"audio"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(nb::step_phase ) override;
    bool event(SDL_Event * ) override;

    // we want to have a simple API for music and sfx,
    // that is easy to invoke via reflection
    // no need for fancy features, this should be good enough for simple games

    // top-level control
    void out_mute(bool muted);
    void out_gain(float gain);

    // background music
    bool bgm_play(entt::id_type res_id);
    bool bgm_playing();
    bool bgm_stop();
    void bgm_gain(float db);
    void set_global_pitch(float ratio);
    void sfx_gain(float db);

    // sound effects
    bool sfx_play(entt::id_type res_id, float gain_db);
    bool sfx_play_pitched(entt::id_type res_id, float gain_db, float pitch_ratio);

private:
    void _draw_tool_window(bool *);
    audio_p* _d {nullptr};
};

}
