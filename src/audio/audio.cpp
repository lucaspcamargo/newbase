#include <newbase/audio/audio.hpp>
#include <newbase/audio/graph_manager.hpp>
#include <newbase/audio/vorbis_feedback.hpp>
#include <newbase/audio/graph/graph.hpp>
#include <newbase/audio/types.hpp>
#include <newbase/engine.hpp>
#include <newbase/sdl/utils.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/vorbis.hpp>
#include <newbase/log.hpp>
#include <newbase/services/ui_manager.hpp>
#include <entt/meta/factory.hpp>
#include <entt/locator/locator.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>

// for debug ui
#include "imgui.h"
#include <newbase/ui/imgui_icons.hpp>

#include <algorithm>
#include <cassert>

using namespace nb;
using entt::operator""_hs;

// ---------------------------------------------------------------------------
// Private data — all per-instance state
// ---------------------------------------------------------------------------
static constexpr size_t VIS_FRAMES = 1024;

struct nb::audio_p {
    // Graph management
    audio_graph_manager gm;
    audio_graph::graph  graph;
    SDL_Mutex*          graph_mtx {nullptr};

    // SDL audio device / stream
    SDL_AudioDeviceID dev_out   {0};
    SDL_AudioSpec     spec_out  {};
    int               bufsize_out {0};
    SDL_AudioStream*  out_stream {nullptr};

    // Output state
    bool  out_mute {false};
    float out_gain {1.0f};

    // Managed buses: each has a display name and a graphplan gain node id.
    struct managed_bus {
        std::string name;
        float       gain_db      {0.f};
        uint64_t    gain_node_id {0};
    };
    // Managed players: each maps to two graphplan nodes (vorbis source + bus input).
    struct managed_player {
        std::string bus_name;
        audio_graph_manager::player_nodes nodes {};
    };

    std::vector<managed_bus>    buses;
    std::vector<managed_player> players;
    float global_pitch {1.0f};

    // Audio-thread → main-thread visualisation buffer
    float      vis_buf[VIS_FRAMES] {};
    size_t     vis_buf_len {0};
    SDL_Mutex* vis_mtx {nullptr};
};

// ---------------------------------------------------------------------------
// Audio thread callback — userdata is audio_p*
// ---------------------------------------------------------------------------
static void audio_out_cb(void* userdata, SDL_AudioStream* stream, int additional_amount, int)
{
    if (!additional_amount) return;
    auto* d = static_cast<audio_p*>(userdata);

    audio_format dev_fmt = audio_format::UNKNOWN;
    switch (d->spec_out.format) {
        case SDL_AUDIO_F32: dev_fmt = audio_format::FLOAT; break;
        case SDL_AUDIO_S16: dev_fmt = audio_format::S16;   break;
        case SDL_AUDIO_U8:  dev_fmt = audio_format::U8;    break;
        case SDL_AUDIO_S8:  dev_fmt = audio_format::S8;    break;
        default: break;
    }
    const size_t dev_frame_sz   = audio_format_size(dev_fmt) * static_cast<size_t>(d->spec_out.channels);
    const size_t float_frame_sz = sizeof(float) * static_cast<size_t>(d->spec_out.channels);
    const size_t frames         = static_cast<size_t>(additional_amount) / dev_frame_sz;
    const size_t float_bytes    = frames * float_frame_sz;

    auto* float_buf = reinterpret_cast<uint8_t*>(alloca(float_bytes));

    SDL_LockMutex(d->graph_mtx);
    d->graph.produce(float_buf, float_bytes);
    SDL_UnlockMutex(d->graph_mtx);

    if (d->out_mute) { SDL_ClearAudioStream(stream); return; }

    if (d->vis_mtx)
    {
        const audio_spec& sp = d->graph.spec();
        const size_t stride = audio_format_size(sp.format) * static_cast<size_t>(sp.channels);
        const size_t n      = stride ? float_bytes / stride : 0;
        const size_t copy   = n < VIS_FRAMES ? n : VIS_FRAMES;
        SDL_LockMutex(d->vis_mtx);
        for (size_t i = 0; i < copy; ++i)
        {
            const uint8_t* s = float_buf + i * stride;
            float v = 0.f;
            switch (sp.format) {
                case audio_format::FLOAT: v = *reinterpret_cast<const float*>(s);             break;
                case audio_format::S16:   v = *reinterpret_cast<const int16_t*>(s) / 32768.f; break;
                case audio_format::S8:    v = *reinterpret_cast<const int8_t*>(s)  / 128.f;   break;
                case audio_format::U8:    v = (*s / 255.f) * 2.f - 1.f;                       break;
                default: break;
            }
            d->vis_buf[i] = v;
        }
        d->vis_buf_len = copy;
        SDL_UnlockMutex(d->vis_mtx);
    }

    SDL_PutAudioStreamData(stream, float_buf, static_cast<int>(float_bytes));
}

// ---------------------------------------------------------------------------
// audio
// ---------------------------------------------------------------------------
audio::audio()
{
    nb::log::info("[audio] constructing");
    _d = new audio_p();
}

audio::~audio()
{
    nb::log::info("[audio] destroying");

    // Stop the audio callback before touching shared state.
    // 1. Clear the callback so no new invocations can start.
    if (_d->out_stream)
        SDL_SetAudioStreamGetCallback(_d->out_stream, nullptr, nullptr);
    // 2. Destroy the stream — SDL3 waits for any in-flight callback to finish
    //    before returning, so the mutexes are safe to destroy afterwards.
    if (_d->out_stream) { SDL_DestroyAudioStream(_d->out_stream); _d->out_stream = nullptr; }
    if (_d->dev_out)    { SDL_CloseAudioDevice(_d->dev_out);       _d->dev_out    = 0;       }

    // No callback can run past this point — safe to swap graph and tear down.
    SDL_LockMutex(_d->graph_mtx);
    _d->graph = audio_graph::graph{};
    SDL_UnlockMutex(_d->graph_mtx);

    _d->gm.shutdown(); // destroy caches and feedback resources

    if (_d->graph_mtx) { SDL_DestroyMutex(_d->graph_mtx); _d->graph_mtx = nullptr; }
    if (_d->vis_mtx)   { SDL_DestroyMutex(_d->vis_mtx);   _d->vis_mtx   = nullptr; }

    delete _d;
    _d = nullptr;
    nb::log::info("[audio] destroyed");
}

SDL_InitFlags audio::sdl_subsystems(ryml::ConstNodeRef)
{
    return SDL_INIT_AUDIO;
}

bool audio::init(ryml::ConstNodeRef cfg)
{
    nb::log::info("[audio] init");

    _d->graph_mtx = SDL_CreateMutex();
    assert(_d->graph_mtx);
    _d->vis_mtx = SDL_CreateMutex();
    assert(_d->vis_mtx);

    const auto drivers = get_all_strings(SDL_GetNumAudioDrivers, SDL_GetAudioDriver);
    nb::log::info("[audio] available drivers: %s", join_strings(drivers, ' ').c_str());

    _d->dev_out = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!_d->dev_out)
    {
        nb::log::error("[audio] could not open default device!");
        return false;
    }

    SDL_GetAudioDeviceFormat(_d->dev_out, &_d->spec_out, &_d->bufsize_out);
    nb::log::info("[audio] using '%s', opened device '%s' (%d channels, %d Hz, %d frames)",
                  SDL_GetCurrentAudioDriver(), SDL_GetAudioDeviceName(_d->dev_out),
                  _d->spec_out.channels, _d->spec_out.freq, _d->bufsize_out);

    audio_spec graph_spec;
    graph_spec.from_sdl(_d->spec_out);
    graph_spec.format = audio_format::FLOAT;
    _d->graph.set_spec(graph_spec);

    SDL_AudioSpec float_sdl;
    graph_spec.to_sdl(float_sdl);
    _d->out_stream = SDL_CreateAudioStream(&float_sdl, &_d->spec_out);
    if (_d->out_stream)
    {
        if (SDL_BindAudioStream(_d->dev_out, _d->out_stream))
        {
            nb::log::info("[audio] output stream live");
            if (SDL_SetAudioStreamGetCallback(_d->out_stream, audio_out_cb, _d))
                nb::log::info("[audio] output stream callback set");
            else
                nb::log::error("[audio] could not set output stream callback!");
        }
        else
            nb::log::error("[audio] could not bind output stream!");
    }
    else
        nb::log::error("[audio] could not create output stream!");

    if (auto* ui_mgr = entt::locator<ui_manager*>::value())
        ui_mgr->register_tool_window("audio", [this](bool* open){ _draw_tool_window(open); });

    engine::instance().debug_action_register("Audio Tools", [](){
        if (auto* ui_mgr = entt::locator<ui_manager*>::value())
            ui_mgr->toggle_tool_window("audio");
    }, 9);

    _d->gm.init(_d->graph.spec());

    // Add default managed buses, reading initial gain values from config.
    auto add_bus = [&](const char* name) {
        float db = 0.f;
        if (cfg.has_child(name)) {
            std::string key = std::string{name} + "_gain_db";
            if (cfg.has_child(key.c_str())) cfg[key.c_str()] >> db;
        }
        // Also try the direct key form used in config (bgm_gain_db / sfx_gain_db).
        std::string cfg_key = std::string{name} + "_gain_db";
        if (cfg.has_child(cfg_key.c_str())) cfg[cfg_key.c_str()] >> db;
        uint64_t gain_id = _d->gm.add_managed_bus(name, db);
        _d->buses.push_back({name, db, gain_id});
    };
    add_bus("bgm");
    add_bus("sfx");

    _d->gm.rebuild(_d->graph, _d->graph_mtx);

    log::info("[audio] initialized");
    return true;
}

bool audio::step(step_phase phase)
{
    if (phase == GENERAL_UPDATE)
    {
        bool any_removed = false;
        for (auto it = _d->players.begin(); it != _d->players.end(); )
        {
            auto fb = _d->gm.get_player_feedback(it->nodes);
            if (fb)
            {
                size_t lc = fb->loop_count.load(std::memory_order_relaxed);
                size_t pc = fb->play_count.load(std::memory_order_relaxed);
                if (lc > 0 && pc >= lc)
                {
                    log::verb("[audio] player finished (bus=%s), removing", it->bus_name.c_str());
                    _d->gm.remove_player(it->nodes);
                    it = _d->players.erase(it);
                    any_removed = true;
                    continue;
                }
            }
            ++it;
        }
        if (any_removed)
            _d->gm.rebuild(_d->graph, _d->graph_mtx);
    }
    return true;
}
bool audio::event(SDL_Event*) { return true; }

void audio::out_mute(bool muted)
{
    _d->out_mute = muted;
    out_gain(_d->out_gain);
}

void audio::out_gain(float gain)
{
    _d->out_gain = gain;
    SDL_SetAudioDeviceGain(_d->dev_out, _d->out_mute ? 0.0f : _d->out_gain);
}

// Helper: find a managed_bus entry by name. Returns nullptr if not found.
static audio_p::managed_bus* find_bus(audio_p* d, const std::string& name)
{
    for (auto& b : d->buses)
        if (b.name == name) return &b;
    return nullptr;
}

// Helper: remove all players on the given bus, then rebuild.
static void stop_bus_players(audio_p* d, audio_graph_manager& gm,
                              const std::string& bus_name)
{
    for (auto it = d->players.begin(); it != d->players.end(); )
    {
        if (it->bus_name == bus_name)
        {
            gm.remove_player(it->nodes);
            it = d->players.erase(it);
        }
        else ++it;
    }
}

bool audio::bgm_play(entt::id_type res_id)
{
    auto vorbis_res = rman().get<rvorbis>(res_id);
    if (!vorbis_res || !vorbis_res->valid)
    {
        log::error("[audio] bgm_play: invalid resource: %x", res_id);
        return false;
    }
    stop_bus_players(_d, _d->gm, "bgm");
    auto bgm_nodes = _d->gm.add_player("bgm", res_id, /*loop=*/true);
    if (_d->global_pitch != 1.0f)
        _d->gm.apply_pitch_ratio(bgm_nodes.pitch_node_id, _d->global_pitch);
    _d->players.push_back({"bgm", bgm_nodes});
    _d->gm.rebuild(_d->graph, _d->graph_mtx);
    log::info("[audio] bgm_play: %x", res_id);
    return true;
}

bool audio::bgm_playing()
{
    for (const auto& p : _d->players)
        if (p.bus_name == "bgm") return true;
    return false;
}

bool audio::bgm_stop()
{
    stop_bus_players(_d, _d->gm, "bgm");
    _d->gm.rebuild(_d->graph, _d->graph_mtx);
    return true;
}

void audio::bgm_gain(float db)
{
    if (auto* b = find_bus(_d, "bgm")) {
        b->gain_db = db;
        _d->gm.apply_gain_db(b->gain_node_id, db);
    }
}

void audio::set_global_pitch(float ratio)
{
    _d->global_pitch = ratio;
    for (auto& p : _d->players)
        _d->gm.apply_pitch_ratio(p.nodes.pitch_node_id, ratio);
}

void audio::sfx_gain(float db)
{
    if (auto* b = find_bus(_d, "sfx")) {
        b->gain_db = db;
        _d->gm.apply_gain_db(b->gain_node_id, db);
    }
}

bool audio::sfx_play(entt::id_type res_id, float gain_db)
{
    auto vorbis_res = rman().get<rvorbis>(res_id);
    if (!vorbis_res || !vorbis_res->valid)
    {
        log::error("[audio] sfx_play: invalid resource: %x", res_id);
        return false;
    }
    auto nodes = _d->gm.add_player("sfx", res_id, /*loop=*/false);
    if (gain_db != 0.f)
        _d->gm.apply_gain_db(nodes.gain_node_id, gain_db);
    if (_d->global_pitch != 1.0f)
        _d->gm.apply_pitch_ratio(nodes.pitch_node_id, _d->global_pitch);
    _d->players.push_back({"sfx", nodes});
    _d->gm.rebuild(_d->graph, _d->graph_mtx);
    log::info("[audio] sfx_play: %x (gain_db=%.1f)", res_id, gain_db);
    return true;
}

bool audio::sfx_play_pitched(entt::id_type res_id, float gain_db, float pitch_ratio)
{
    auto vorbis_res = rman().get<rvorbis>(res_id);
    if (!vorbis_res || !vorbis_res->valid)
    {
        log::error("[audio] sfx_play_pitched: invalid resource: %x", res_id);
        return false;
    }
    auto nodes = _d->gm.add_player("sfx", res_id, /*loop=*/false);
    if (gain_db != 0.f)
        _d->gm.apply_gain_db(nodes.gain_node_id, gain_db);
    _d->gm.apply_pitch_ratio(nodes.pitch_node_id, pitch_ratio * _d->global_pitch);
    _d->players.push_back({"sfx", nodes});
    _d->gm.rebuild(_d->graph, _d->graph_mtx);
    log::info("[audio] sfx_play_pitched: %x (gain_db=%.1f pitch=%.2f)", res_id, gain_db, pitch_ratio);
    return true;
}

// ---------------------------------------------------------------------------
// Tool window
// ---------------------------------------------------------------------------
void audio::_draw_tool_window(bool* close)
{
    ImVec2 slider_size{25, 120};

    ImGui::Begin(ICON_KI_SOUND_ON " Audio", close);

    if (ImGui::TreeNodeEx("Mixer", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox(ICON_KI_SOUND_OFF " Out Mute", &_d->out_mute))
            out_mute(_d->out_mute);
        if (ImGui::VSliderFloat("OUT", slider_size, &_d->out_gain, 0.f, 1.f, "%.1f"))
            out_gain(_d->out_gain);
        for (auto& bus : _d->buses)
        {
            ImGui::SameLine();
            ImGui::PushID(bus.name.c_str());
            if (ImGui::VSliderFloat(bus.name.c_str(), slider_size, &bus.gain_db, -60.f, 6.f, "%.0f"))
                _d->gm.apply_gain_db(bus.gain_node_id, bus.gain_db);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Output", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static float   vis_snapshot[VIS_FRAMES]{};
        static size_t  vis_snapshot_len{0};
        SDL_LockMutex(_d->vis_mtx);
        if (_d->vis_buf_len)
        {
            std::copy(_d->vis_buf, _d->vis_buf + _d->vis_buf_len, vis_snapshot);
            vis_snapshot_len = _d->vis_buf_len;
        }
        SDL_UnlockMutex(_d->vis_mtx);

        if (vis_snapshot_len)
            ImGui::PlotLines("##waveform", vis_snapshot, static_cast<int>(vis_snapshot_len),
                             0, nullptr, -1.f, 1.f,
                             ImVec2(ImGui::GetContentRegionAvail().x, 60.f));
        else
            ImGui::TextDisabled("(no output)");
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Graphplan", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static bool auto_rebuild = true;
        ImGui::Checkbox("Auto-rebuild", &auto_rebuild);
        if(!auto_rebuild)
        {
            ImGui::SameLine();
            if (ImGui::Button("Rebuild Graph"))
                _d->gm.rebuild(_d->graph, _d->graph_mtx);
        }
        if (_d->gm.draw_editor() && auto_rebuild)
            _d->gm.rebuild(_d->graph, _d->graph_mtx);
        ImGui::TreePop();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// RTTI
// ---------------------------------------------------------------------------
extern "C" void _rtti_init_audio()
{
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
        .func<&audio::bgm_gain>("bgm_gain"_hs)
        .custom<rtti::func_info>(rtti::func_info{"bgm_gain"})
        .func<&audio::set_global_pitch>("set_global_pitch"_hs)
        .custom<rtti::func_info>(rtti::func_info{"set_global_pitch"})
        .func<&audio::sfx_play>("sfx_play"_hs)
        .custom<rtti::func_info>(rtti::func_info{"sfx_play"})
        .func<&audio::sfx_play_pitched>("sfx_play_pitched"_hs)
        .custom<rtti::func_info>(rtti::func_info{"sfx_play_pitched"});

    entt::meta_factory<std::shared_ptr<nb::audio>>{rtti::ctx_systems()}
        .type("audio_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::audio>>()
        .conv<std::shared_ptr<nb::system>>();
}
