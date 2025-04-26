#include <newbase/audio/audio.h>
#include <newbase/audio/producer/buffer.h>
#include <newbase/audio/producer/looper.h>
#include <newbase/audio/producer/vorbis.h>
#include <newbase/engine.h>
#include <newbase/sdl/utils.h>
#include <newbase/reflection/contexts.h>
#include <newbase/reflection/data.h>
#include <newbase/res/manager.h>
#include <newbase/res/vorbis.h>
#include <newbase/log.h>
#include <entt/meta/factory.hpp>

// for debug ui
#include "imgui.h"
#include "IconsForkAwesome.h"

using namespace nb;
using entt::operator""_hs;

static SDL_AudioDeviceID _dev_out{ 0 };
static SDL_AudioSpec _spec_out;
static int _bufsize_out; 

bool _out_mute {false};
float _out_gain {1.0f};

SDL_AudioStream *_bgm {nullptr};
audio_producer * _bgm_prod {nullptr};
float _bgm_gain {1.0f};
float _sfx_gain {1.0f};

static bool _show_debug_ui {false};

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

    if(cfg.has_child("bgm_gain"))
    {
        cfg["bgm_gain"] >> _bgm_gain; 
    }

    if(cfg.has_child("sfx_gain"))
    {
        cfg["sfx_gain"] >> _sfx_gain; 
    }

    engine::instance().debug_action_register("audio debug toggle", [](){
        _show_debug_ui = !_show_debug_ui;
    }, 9);

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

        if(_show_debug_ui)
            show_debug_ui(&_show_debug_ui);
    }
    return true;
}


bool audio::event(SDL_Event*)
{
    return true;
}

void audio::out_mute(bool muted)
{
    _out_mute = muted;
    out_gain(_out_gain);
}

void audio::out_gain(float gain)
{
    _out_gain = gain;
    SDL_SetAudioDeviceGain(_dev_out, _out_mute? 0.0f: _out_gain);
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

    SDL_AudioSpec spec;
    vorbis_res->spec.to_sdl(spec);
    _bgm = SDL_CreateAudioStream(&spec, nullptr);
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
bool audio::sfx_play(entt::id_type res_id, float gain)
{
    return false;
}

// debug
void audio::show_debug_ui(bool *close)
{
    ImGui::Begin(ICON_FK_VOLUME_UP " Audio", close);
    if(ImGui::Checkbox("Out Mute", &_out_mute))
        out_mute(_out_mute);
    if(ImGui::SliderFloat("Out Gain", &_out_gain, 0.f, 1.f))
        out_gain(_out_gain);
    if(ImGui::SliderFloat("BGM Gain", &_bgm_gain, 0.f, 1.f))
        bgm_gain(_bgm_gain);
    if(ImGui::SliderFloat("SFX Gain", &_sfx_gain, 0.f, 1.f))
        ;//sfx_gain(_sfx_gain);
    ImGui::End();
}

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