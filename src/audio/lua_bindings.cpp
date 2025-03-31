#include <newbase/audio/audio.h>
#include <sol/sol.hpp>

using namespace nb;

void audio::bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    lua["system_audio"] = this->metatype_id();
    lua["audio_bgm_play"] = [this](entt::id_type id) -> void {
        this->bgm_play(id);
    };
    lua["audio_bgm_playing"] = [this]() -> bool {
        return this->bgm_playing();
    };
    lua["audio_bgm_stop"] = [this]() -> bool {
        return this->bgm_stop();
    };
    lua["audio_bgm_gain"] = [this](float gain) -> void {
        this->bgm_gain(gain);
    };
}