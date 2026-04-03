#include <newbase/audio/audio.hpp>
#include <newbase/audio/producer/buffer.hpp>
#include <newbase/audio/producer/looper.hpp>
#include <newbase/audio/producer/vorbis.hpp>
#include <newbase/audio/graph/graph.hpp>
#include <newbase/engine.hpp>
#include <newbase/sdl/utils.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/vorbis.hpp>
#include <newbase/log.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/graphplan/plan.hpp>
#include <newbase/graphplan/editor.hpp>
#include <entt/meta/factory.hpp>
#include <entt/locator/locator.hpp>

// for debug ui
#include "imgui.h"
#include "imgui_node_editor.h"
#include <newbase/ui/imgui_icons.hpp>

using namespace nb;
using entt::operator""_hs;

static SDL_AudioDeviceID _dev_out{ 0 };
static SDL_AudioSpec _spec_out;
static int _bufsize_out; 

bool _out_mute {false};
float _out_gain {1.0f};
SDL_AudioStream * _out_stream {nullptr};

SDL_Mutex * _graph_mtx;
audio_graph::graph _graph;


SDL_AudioStream *_bgm {nullptr};
audio_producer * _bgm_prod {nullptr};
float _bgm_gain {1.0f};
float _sfx_gain {1.0f};

graphplan::plan * _graphplan {nullptr};
graphplan::editor * _graphplan_editor {nullptr};

// callbacks
static void audio_out_cb(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    // WARN: called from audio thread!

    if(!additional_amount)
    {
        // stream already has all requested frames
        return;
    }

    auto self = static_cast<audio*>(userdata);
    assert(self);

    // keep stream empty when muted
    if(_out_mute)
    {
        SDL_ClearAudioStream(stream);
        return;
    }

    uint8_t* buf = reinterpret_cast<uint8_t*>(alloca(sizeof(uint8_t) * additional_amount));

    // keep critical section as short as possible 
    SDL_LockMutex(_graph_mtx);
    _graph.produce(buf, static_cast<size_t>(additional_amount));
    SDL_UnlockMutex(_graph_mtx);

    SDL_PutAudioStreamData(stream, buf, static_cast<size_t>(additional_amount));
}


audio::audio()
{
    nb::log::info("[audio] constructing");
}

audio::~audio()
{
    nb::log::info("[audio] destroying");
    if(_graphplan_editor)
    {
        delete _graphplan_editor;
        _graphplan_editor = nullptr;
    }
    if(_graphplan)
    {
        delete _graphplan;
        _graphplan = nullptr;
    }
    if(_bgm)
    {
        SDL_DestroyAudioStream(_bgm);
        _bgm = 0;
    }
    if(_dev_out)
    {
        SDL_CloseAudioDevice(_dev_out);
        _dev_out = 0;
    }
    nb::log::info("[audio] destroyed");
}

SDL_InitFlags audio::sdl_subsystems(ryml::ConstNodeRef)
{
    return SDL_INIT_AUDIO;
}

bool audio::init(ryml::ConstNodeRef cfg)
{
    nb::log::info("[audio] init");

    _graph_mtx = SDL_CreateMutex();
    assert(_graph_mtx);

    const auto drivers = get_all_strings(SDL_GetNumAudioDrivers, SDL_GetAudioDriver);
    const auto drivers_str = join_strings(drivers, ' ');
    nb::log::info("[audio] available drivers: %s", drivers_str.c_str());
    
    _dev_out = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if(!_dev_out)
    {
        nb::log::error("[audio] could not open default device!");
        return false;
    }
    else
    {
        SDL_GetAudioDeviceFormat(_dev_out, &_spec_out, &_bufsize_out);
        nb::log::info("[audio] using '%s', opened device '%s' (%d channels, %d Hz, %d frames)", SDL_GetCurrentAudioDriver(), SDL_GetAudioDeviceName(_dev_out), _spec_out.channels, _spec_out.freq, _bufsize_out);
        _out_stream = SDL_CreateAudioStream(&_spec_out, &_spec_out);
        if(_out_stream)
        {
            if(SDL_BindAudioStream(_dev_out, _out_stream))
            {
                nb::log::info("[audio] output stream live");
                if(SDL_SetAudioStreamGetCallback(_out_stream, audio_out_cb, this))
                {
                    nb::log::info("[audio] output stream callback set");
                }
                else
                {
                    nb::log::error("[audio] could not set output stream callback!");    
                }
            }
            else
            {
                nb::log::error("[audio] could not bind output stream!");
            }
        }
        else
        {
            nb::log::error("[audio] could not create output stream!");
        }
    }

    if(cfg.has_child("bgm_gain"))
    {
        cfg["bgm_gain"] >> _bgm_gain; 
    }

    if(cfg.has_child("sfx_gain"))
    {
        cfg["sfx_gain"] >> _sfx_gain; 
    }

    ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
    if(ui_mgr)
    {
        ui_mgr->register_tool_window("audio", [this](bool *open){
            _draw_tool_window(open);
        });
    }

    engine::instance().debug_action_register("audio debug toggle", [](){
        ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
        if(ui_mgr)
            ui_mgr->toggle_tool_window("audio");
    }, 9);

    _init_graphplan();
    
    log::info("[audio] initialized");
    return true;
}


bool audio::step(step_phase phase)
{

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
    auto vorbis_res = rman().get<rvorbis>(res_id);
    if(!vorbis_res->valid)
    {
        log::error("[audio] bgm_play: invalid resource: %x", res_id);
        return false;
    }

    log::info("[audio] bgm_play: %x", res_id);
    return false;
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
void audio::_draw_tool_window(bool *close)
{
    ImVec2 slider_size {25, 120};

    ImGui::Begin(ICON_KI_SOUND_ON " Audio", close);

    if(ImGui::TreeNodeEx("Mixer", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if(ImGui::Checkbox(ICON_KI_SOUND_OFF " Out Mute", &_out_mute))
            out_mute(_out_mute);
        if(ImGui::VSliderFloat("OUT", slider_size, &_out_gain, 0.f, 1.f, "%.1f"))
            out_gain(_out_gain);
        ImGui::SameLine();
        if(ImGui::VSliderFloat("BGM", slider_size, &_bgm_gain, 0.f, 1.f, "%.1f"))
            bgm_gain(_bgm_gain);
        ImGui::SameLine();
        if(ImGui::VSliderFloat("SFX", slider_size, &_sfx_gain, 0.f, 1.f, "%.1f"))
            ;//sfx_gain(_sfx_gain);

        ImGui::TreePop();
    }

    if(ImGui::TreeNodeEx("Graphplan", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if(!_graphplan_editor)
        {
            _graphplan_editor = new graphplan::editor(*_graphplan);
        }
        _graphplan_editor->draw();
        ImGui::TreePop();
    }

    ImGui::End();
}

void audio::_init_graphplan()
{
    log::info("[audio] creating graphplan");
    _graphplan = new graphplan::plan(graphplan::domain{"audio_graph", {0}, true});
    // sound output node
    uint64_t node_id = _graphplan->get_next_unique_id();
    uint64_t pin_id = node_id + 1;
    _graphplan->nodes.insert({node_id, graphplan::node_data{node_id, 0, {pin_id}, {}, 0, 0}});
    _graphplan->pins.insert({pin_id, graphplan::pin_data{pin_id, node_id}});
}

// RTTI metadata
extern "C" void _rtti_init_audio()
{
    // main interface
    entt::meta_factory<nb::audio>{}
        .type("audio"_hs)
        .custom<rtti::type_info>(rtti::type_info{"audio", rtti::TYPE_CLASS_SYSTEM})
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