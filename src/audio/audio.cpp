#include <newbase/audio/audio.h>
#include <newbase/sdl/utils.h>
#include <newbase/reflection/contexts.h>
#include <newbase/reflection/data.h>
#include <newbase/res/manager.h>
#include <newbase/res/vorbis.h>
#include <newbase/log.h>
#include <entt/meta/factory.hpp>

using namespace nb;
using entt::operator""_hs;

static SDL_AudioDeviceID _dev_out{ 0 };
static SDL_AudioSpec _spec_out;
static int _bufsize_out; 

SDL_AudioStream *_bgm {nullptr};
float _bgm_gain {1.0f};
float _sfx_gain {1.0f};

audio::audio()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] constructing");
}

audio::~audio()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] destroying");
    if(_bgm)
    {
        SDL_DestroyAudioStream(_bgm);
    }
    if(_dev_out)
    {
        SDL_CloseAudioDevice(_dev_out);
        _dev_out = 0;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] destroyed");
}

SDL_InitFlags audio::sdl_subsystems(ryml::ConstNodeRef)
{
    return SDL_INIT_AUDIO;
}

bool audio::init(ryml::ConstNodeRef cfg)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] init");

    const auto drivers = get_all_strings(SDL_GetNumAudioDrivers, SDL_GetAudioDriver);
    const auto drivers_str = join_strings(drivers, ' ');
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] available drivers: %s", drivers_str.c_str());
    
    _dev_out = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if(!_dev_out)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[audio] could not open default device!");
        return false;
    }
    else
    {
        SDL_GetAudioDeviceFormat(_dev_out, &_spec_out, &_bufsize_out);
        SDL_GetCurrentAudioDriver();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] using '%s', opened device '%s' (%d channels, %d Hz, %d frames)", SDL_GetCurrentAudioDriver(), SDL_GetAudioDeviceName(_dev_out), _spec_out.channels, _spec_out.freq, _bufsize_out);
        SDL_ResumeAudioDevice(_dev_out);
    }

    if(!cfg["bgm_gain"].invalid())
    {
        cfg["bgm_gain"] >> _bgm_gain; 
    }

    if(!cfg["sfx_gain"].invalid())
    {
        cfg["sfx_gain"] >> _sfx_gain; 
    }

    return true;
}


bool audio::step(step_phase phase)
{
    if(phase == step_phase::POST_UPDATE)
    {
        // some test code for audio goes here, hardcoded
        static bool first = true;
        if(first)
        {
            first = false;
        }
    }
    return true;
}


bool audio::event(SDL_Event*)
{
    return true;
}

bool audio::bgm_play(entt::id_type res_id)
{
    if(_bgm)
    {
        SDL_DestroyAudioStream(_bgm);
        _bgm = nullptr; 
    }

    auto vorbis_res = rman().get_vorbis(res_id);
    if(!vorbis_res->valid)
    {
        log::error("[audio] bgm_play: invalid resource: %x", res_id);
        return false;
    }

    _bgm = SDL_CreateAudioStream(&(vorbis_res->spec), nullptr);
    if(!_bgm)
    {
        log::error("[audio] bgm_play: cannot open stream: %s", SDL_GetError());
        return false;
    }
    
    if(!SDL_BindAudioStream(_dev_out, _bgm))
    {
        log::error("[audio] bgm_play: cannot bind stream: %s", SDL_GetError());
        return false;
    } 
 
    if(!SDL_PutAudioStreamData(_bgm, vorbis_res->frames.data(), vorbis_res->frames.size()))
    {
        log::error("[audio] bgm_play: cannot enqueue data: %s", SDL_GetError());
        return false;
    } 

    if(!SDL_SetAudioStreamGain(_bgm, _bgm_gain))
    {
        log::error("[audio] bgm_play: cannot set stream gain: %s", SDL_GetError());
    }

    log::info("[audio] bgm_play: %x", res_id);
    return true;
}

bool audio::bgm_playing()
{
    return static_cast<bool>(_bgm);
}

bool audio::bgm_stop()
{
    return false;
}

void audio::bgm_gain(float gain)
{
    if(_bgm)
    {
        SDL_SetAudioStreamGain(_bgm, gain);
    }
    _bgm_gain = gain;
}

// sound effects 
bool sfx_play(entt::id_type res_id, float gain);

// RTTI metadata
extern "C" void _rtti_init_audio()
{
    // main interface
    entt::meta_factory<nb::audio>{}
        .type("audio"_hs)
        .custom<rtti::system_info>(rtti::system_info{"audio"})
        .base<nb::system>()
        .func<&audio::bgm_play>("bgm_play"_hs)
        .custom<rtti::func_info>(rtti::func_info{"bgm_play"})
        .func<&audio::bgm_playing>("bgm_playing"_hs)
        .custom<rtti::func_info>(rtti::func_info{"bgm_playing"})
        .func<&audio::bgm_stop>("bgm_stop"_hs)
        .custom<rtti::func_info>(rtti::func_info{"bgm_stop"})
        .func<&audio::bgm_stop>("bgm_gain"_hs)
        .custom<rtti::func_info>(rtti::func_info{"bgm_gain"})
        .func<&audio::bgm_stop>("sfx_play"_hs)
        .custom<rtti::func_info>(rtti::func_info{"sfx_play"});

    // factory
    entt::meta_factory<std::shared_ptr<nb::audio>>{rtti::ctx_systems()}
        .type("audio_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::audio>>()
        .conv<std::shared_ptr<nb::system>>();
}