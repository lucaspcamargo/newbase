#include <newbase/audio/audio.hpp>
#include <algorithm>
#include <newbase/audio/producer/buffer.hpp>
#include <newbase/audio/producer/looper.hpp>
#include <newbase/audio/producer/vorbis.hpp>
#include <newbase/audio/producer/generators.hpp>
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

// Feedback buffer: audio thread writes, main thread reads for visualization.
static constexpr size_t VIS_FRAMES = 1024;
static float     _vis_buf[VIS_FRAMES] {};
static size_t    _vis_buf_len {0};
static SDL_Mutex* _vis_mtx {nullptr};


SDL_AudioStream *_bgm {nullptr};
audio_producer * _bgm_prod {nullptr};
float _bgm_gain_db {0.f};
float _sfx_gain_db {0.f};

// Graphplan node IDs for the built-in BGM/SFX gain nodes (set in _init_graphplan).
static uint64_t _bgm_gain_node_id {0};
static uint64_t _sfx_gain_node_id {0};

graphplan::plan * _graphplan {nullptr};
graphplan::editor * _graphplan_editor {nullptr};

// Persistent node cache: graphplan node id → live audio_graph node instance.
// Reused across rebuilds so stateful nodes (vorbis, reverb) keep their state.
static std::unordered_map<uint64_t, std::shared_ptr<audio_graph::node>> _node_cache;
// Tracks which res_id string each cached vorbis node was built with.
static std::unordered_map<uint64_t, std::string> _vorbis_res_cache;
// Bus mixer cache: bus_id string → shared mixer_node (shared by all bus inputs/outputs).
static std::unordered_map<std::string, std::shared_ptr<audio_graph::mixer_node>> _bus_cache;
// Monotonically increasing id counter — never reuses ids across rebuilds.
static audio_graph::node_id _next_ag_id {1}; // 0 is always output

// Pin type IDs for the audio domain (opaque — the graphplan treats them as identifiers).
static constexpr int AUDIO_PIN_STREAM = 1; // an audio stream connection

// Audio graph domain — defines the node types available in the graphplan editor.
// Node type_ids match audio_graph::node_type enum values.
static const graphplan::domain AUDIO_DOMAIN = []() {
    using namespace graphplan;
    const int OUT = static_cast<int>(audio_graph::node_type::OUTPUT);
    const int SRC = static_cast<int>(audio_graph::node_type::SOURCE);
    const int MIX = static_cast<int>(audio_graph::node_type::MIXER);
    const int VBR = static_cast<int>(audio_graph::node_type::VORBIS_SOURCE);
    const int SIN = static_cast<int>(audio_graph::node_type::SINE_SOURCE);
    const int NOI = static_cast<int>(audio_graph::node_type::NOISE_SOURCE);
    enum { CAT_NONE=0, CAT_SOURCE=1, CAT_ROUTING=2, CAT_FX=3, CAT_BUS=4 };

    domain d;
    d.id      = "audio_graph";
    d.acyclic = true;
    d.pin_types  = { pin_type_def{AUDIO_PIN_STREAM, "Audio", true} };
    d.categories = {
        category_def{CAT_SOURCE,  "Sources"},
        category_def{CAT_ROUTING, "Routing"},
        category_def{CAT_FX,      "Effects"},
        category_def{CAT_BUS,     "Bus"},
    };
    d.node_types = {
        node_type_def{OUT, "Output", {AUDIO_PIN_STREAM}, {}, {}, false, CAT_ROUTING},
        node_type_def{SRC, "Source", {}, {AUDIO_PIN_STREAM}, {}, true,  CAT_SOURCE},
        node_type_def{MIX, "Mixer",  {AUDIO_PIN_STREAM, AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM}, {}, true, CAT_ROUTING},
        node_type_def{VBR, "Vorbis", {}, {AUDIO_PIN_STREAM},
                      { prop_def{"res_id", entt::meta_any{std::string{}}}, prop_def{"loop", entt::meta_any{false}} },
                      true, CAT_SOURCE},
        node_type_def{SIN, "Sine",   {}, {AUDIO_PIN_STREAM},
                      { prop_def{"frequency", entt::meta_any{440.f}}, prop_def{"amplitude", entt::meta_any{0.5f}} },
                      true, CAT_SOURCE},
        node_type_def{NOI, "Noise",  {}, {AUDIO_PIN_STREAM},
                      { prop_def{"amplitude", entt::meta_any{0.5f}} },
                      true, CAT_SOURCE},
        node_type_def{static_cast<int>(audio_graph::node_type::REVERB), "Reverb",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"room_size", entt::meta_any{0.5f}}, prop_def{"damping", entt::meta_any{0.5f}}, prop_def{"wet", entt::meta_any{0.33f}} },
                      true, CAT_FX},
        node_type_def{static_cast<int>(audio_graph::node_type::GAIN), "Gain",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"gain_db", entt::meta_any{0.f}} },
                      true, CAT_ROUTING},
        node_type_def{static_cast<int>(audio_graph::node_type::COMPRESSOR), "Compressor",
                      {AUDIO_PIN_STREAM, AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"threshold_db", entt::meta_any{-18.f}},
                        prop_def{"ratio",        entt::meta_any{4.f}},
                        prop_def{"attack_ms",    entt::meta_any{10.f}},
                        prop_def{"release_ms",   entt::meta_any{100.f}},
                        prop_def{"makeup_db",    entt::meta_any{0.f}} },
                      true, CAT_FX},
        node_type_def{static_cast<int>(audio_graph::node_type::EQ5), "EQ5",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"band0_freq",     entt::meta_any{80.f},    true},
                        prop_def{"band0_gain_db",  entt::meta_any{0.f},     true},
                        prop_def{"band0_q",        entt::meta_any{0.707f},  false},
                        prop_def{"band1_freq",     entt::meta_any{250.f},   true},
                        prop_def{"band1_gain_db",  entt::meta_any{0.f},     true},
                        prop_def{"band1_q",        entt::meta_any{0.707f},  false},
                        prop_def{"band2_freq",     entt::meta_any{1000.f},  true},
                        prop_def{"band2_gain_db",  entt::meta_any{0.f},     true},
                        prop_def{"band2_q",        entt::meta_any{0.707f},  false},
                        prop_def{"band3_freq",     entt::meta_any{4000.f},  true},
                        prop_def{"band3_gain_db",  entt::meta_any{0.f},     true},
                        prop_def{"band3_q",        entt::meta_any{0.707f},  false},
                        prop_def{"band4_freq",     entt::meta_any{12000.f}, true},
                        prop_def{"band4_gain_db",  entt::meta_any{0.f},     true},
                        prop_def{"band4_q",        entt::meta_any{0.707f},  false} },
                      true, CAT_FX},
        node_type_def{static_cast<int>(audio_graph::node_type::BUS_INPUT),  "Bus Input",
                      {AUDIO_PIN_STREAM}, {},
                      { prop_def{"bus_id", entt::meta_any{std::string{}}} },
                      true, CAT_BUS},
        node_type_def{static_cast<int>(audio_graph::node_type::BUS_OUTPUT), "Bus Output",
                      {}, {AUDIO_PIN_STREAM},
                      { prop_def{"bus_id", entt::meta_any{std::string{}}} },
                      true, CAT_BUS},
    };

    // Per-type header colors.
    auto set_color = [&](audio_graph::node_type nt, ImVec4 color) {
        auto it = std::find_if(d.node_types.begin(), d.node_types.end(),
            [nt](const graphplan::node_type_def& t){ return t.type_id == static_cast<int>(nt); });
        if (it != d.node_types.end())
        {
            it->header_color[0] = color.x; it->header_color[1] = color.y;
            it->header_color[2] = color.z; it->header_color[3] = color.w;
        }
    };

    static constexpr float COMMON_ALPHA = 0.25f;
    static constexpr ImVec4 CORE_COLOR(0.0f, 0.0f, 1.0f, COMMON_ALPHA);
    static constexpr ImVec4 BUS_COLOR(0.0f, 1.0f, 1.0f, COMMON_ALPHA);
    static constexpr ImVec4 FX_COLOR(1.0f, 0.0f, 1.0f, COMMON_ALPHA);
    static constexpr ImVec4 SOURCE_COLOR(1.0f, 1.0f, 0.0f, COMMON_ALPHA);


    set_color(audio_graph::node_type::OUTPUT,        CORE_COLOR);
    set_color(audio_graph::node_type::SOURCE,        CORE_COLOR);
    set_color(audio_graph::node_type::MIXER,         CORE_COLOR);
    set_color(audio_graph::node_type::VORBIS_SOURCE, SOURCE_COLOR);
    set_color(audio_graph::node_type::SINE_SOURCE,   SOURCE_COLOR);
    set_color(audio_graph::node_type::NOISE_SOURCE,  SOURCE_COLOR);
    set_color(audio_graph::node_type::REVERB,        FX_COLOR);
    set_color(audio_graph::node_type::BUS_INPUT,     BUS_COLOR);
    set_color(audio_graph::node_type::BUS_OUTPUT,    BUS_COLOR);
    set_color(audio_graph::node_type::GAIN,          FX_COLOR);
    set_color(audio_graph::node_type::COMPRESSOR,    FX_COLOR);
    set_color(audio_graph::node_type::EQ5,           FX_COLOR);

    // EQ5 custom draw: 5 vertical gain sliders with frequency input below each.
    auto eq5_it = std::find_if(d.node_types.begin(), d.node_types.end(),
        [](const graphplan::node_type_def& t){ return t.type_id == static_cast<int>(audio_graph::node_type::EQ5); });
    if (eq5_it != d.node_types.end())
    {
        eq5_it->draw_fn = [](graphplan::node_data& nd) -> bool {
            static constexpr float SLIDER_W  = 36.f;
            static constexpr float SLIDER_H  = 100.f;
            static constexpr float FREQ_W    = 56.f;
            static constexpr float COL_W     = 60.f;
            static constexpr float GAIN_MIN  = -24.f;
            static constexpr float GAIN_MAX  =  24.f;

            bool changed = false;
            for (int b = 0; b < 5; ++b)
            {
                char freq_key[32], gain_key[32];
                snprintf(freq_key, sizeof(freq_key), "band%d_freq",    b);
                snprintf(gain_key, sizeof(gain_key), "band%d_gain_db", b);

                float* freq_p = nd.properties[freq_key].try_cast<float>();
                float* gain_p = nd.properties[gain_key].try_cast<float>();
                if (!freq_p || !gain_p) continue;

                ImGui::PushID(b);
                if (b > 0) ImGui::SameLine(0.f, (COL_W - SLIDER_W) * 0.5f);
                ImGui::BeginGroup();

                // Centre the slider within the column.
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (COL_W - SLIDER_W) * 0.5f);
                float gain = *gain_p;
                if (ImGui::VSliderFloat("##g", ImVec2(SLIDER_W, SLIDER_H), &gain, GAIN_MIN, GAIN_MAX, ""))
                    { nd.properties[gain_key] = entt::meta_any{gain}; changed = true; }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%.1f dB", gain);

                // Frequency text input.
                float freq = *freq_p;
                ImGui::SetNextItemWidth(FREQ_W);
                if (ImGui::InputFloat("##f", &freq, 0.f, 0.f, "%.0f"))
                    { nd.properties[freq_key] = entt::meta_any{std::max(20.f, freq)}; changed = true; }

                ImGui::EndGroup();
                ImGui::PopID();
            }
            return changed;
        };
    }

    return d;
}();

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

    // Calculate frames and float bytes needed
    audio_format dev_fmt = audio_format::UNKNOWN;
    switch(_spec_out.format) {
        case SDL_AUDIO_F32: dev_fmt = audio_format::FLOAT; break;
        case SDL_AUDIO_S16: dev_fmt = audio_format::S16; break;
        case SDL_AUDIO_U8: dev_fmt = audio_format::U8; break;
        case SDL_AUDIO_S8: dev_fmt = audio_format::S8; break;
        default: break;
    }
    const size_t device_frame_size = audio_format_size(dev_fmt) * static_cast<size_t>(_spec_out.channels);
    const size_t float_frame_size = sizeof(float) * static_cast<size_t>(_spec_out.channels);
    const size_t frames = static_cast<size_t>(additional_amount) / device_frame_size;
    const size_t float_bytes = frames * float_frame_size;

    uint8_t* float_buf = reinterpret_cast<uint8_t*>(alloca(sizeof(uint8_t) * float_bytes));

    // keep critical section as short as possible 
    SDL_LockMutex(_graph_mtx);
    _graph.produce(float_buf, float_bytes);
    SDL_UnlockMutex(_graph_mtx);

    // keep stream empty when muted
    if(_out_mute)
    {
        SDL_ClearAudioStream(stream);
        return;
    }
    
    // Update visualization buffer — downmix channel 0 to float.
    if (_vis_mtx)
    {
        const audio_spec& sp = _graph.spec();
        const size_t frame_stride = audio_format_size(sp.format) * static_cast<size_t>(sp.channels);
        const size_t n_frames = frame_stride ? float_bytes / frame_stride : 0;
        const size_t copy_frames = n_frames < VIS_FRAMES ? n_frames : VIS_FRAMES;
        SDL_LockMutex(_vis_mtx);
        for (size_t i = 0; i < copy_frames; ++i)
        {
            const uint8_t* sample = float_buf + i * frame_stride;
            float v = 0.f;
            switch (sp.format)
            {
                case audio_format::FLOAT:
                    v = *reinterpret_cast<const float*>(sample); break;
                case audio_format::S16:
                    v = *reinterpret_cast<const int16_t*>(sample) / 32768.f; break;
                case audio_format::S8:
                    v = *reinterpret_cast<const int8_t*>(sample) / 128.f; break;
                case audio_format::U8:
                    v = (*sample / 255.f) * 2.f - 1.f; break;
                default: break;
            }
            _vis_buf[i] = v;
        }
        _vis_buf_len = copy_frames;
        SDL_UnlockMutex(_vis_mtx);
    }

    SDL_PutAudioStreamData(stream, float_buf, static_cast<int>(float_bytes));
}


audio::audio()
{
    nb::log::info("[audio] constructing");
}

audio::~audio()
{
    nb::log::info("[audio] destroying");

    // lock the graph and clear it
    SDL_LockMutex(_graph_mtx);
     _graph = audio_graph::graph();
    SDL_UnlockMutex(_graph_mtx);
    // this should release resources held by nodes

    // clear the node cache
    _node_cache.clear();
    _vorbis_res_cache.clear();
    _bus_cache.clear();

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
    _vis_mtx = SDL_CreateMutex();
    assert(_vis_mtx);

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
        audio_spec graph_spec;
        graph_spec.from_sdl(_spec_out);
        graph_spec.format = audio_format::FLOAT;  // Always process in float internally
        _graph.set_spec(graph_spec);
        SDL_AudioSpec float_sdl;
        graph_spec.to_sdl(float_sdl);
        _out_stream = SDL_CreateAudioStream(&float_sdl, &_spec_out);
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

    if(cfg.has_child("bgm_gain_db"))
    {
        cfg["bgm_gain_db"] >> _bgm_gain_db;
    }

    if(cfg.has_child("sfx_gain_db"))
    {
        cfg["sfx_gain_db"] >> _sfx_gain_db;
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

static void _apply_gain_db(uint64_t node_id, float db)
{
    if (!node_id || !_graphplan) return;
    _graphplan->nodes.at(node_id).properties["gain_db"] = entt::meta_any{db};
    auto it = _node_cache.find(node_id);
    if (it != _node_cache.end())
        if (auto* gn = dynamic_cast<audio_graph::gain_node*>(it->second.get()))
            gn->set_gain_db(db);
}

void audio::bgm_gain(float db)
{
    _bgm_gain_db = db;
    _apply_gain_db(_bgm_gain_node_id, db);
}

void audio::sfx_gain(float db)
{
    _sfx_gain_db = db;
    _apply_gain_db(_sfx_gain_node_id, db);
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
        if(ImGui::VSliderFloat("BGM", slider_size, &_bgm_gain_db, -60.f, 6.f, "%.0f"))
            bgm_gain(_bgm_gain_db);
        ImGui::SameLine();
        if(ImGui::VSliderFloat("SFX", slider_size, &_sfx_gain_db, -60.f, 6.f, "%.0f"))
            sfx_gain(_sfx_gain_db);

        ImGui::TreePop();
    }

    if(ImGui::TreeNodeEx("Output", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static float vis_snapshot[VIS_FRAMES] {};
        static size_t vis_snapshot_len {0};
        SDL_LockMutex(_vis_mtx);
        if (_vis_buf_len)
        {
            std::copy(_vis_buf, _vis_buf + _vis_buf_len, vis_snapshot);
            vis_snapshot_len = _vis_buf_len;
        }
        SDL_UnlockMutex(_vis_mtx);

        if (vis_snapshot_len)
            ImGui::PlotLines("##waveform", vis_snapshot, static_cast<int>(vis_snapshot_len),
                             0, nullptr, -1.f, 1.f,
                             ImVec2(ImGui::GetContentRegionAvail().x, 60.f));
        else
            ImGui::TextDisabled("(no output)");

        ImGui::TreePop();
    }

    if(ImGui::TreeNodeEx("Graphplan", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if(!_graphplan_editor)
        {
            _graphplan_editor = new graphplan::editor(*_graphplan);
        }
        static bool auto_rebuild = true;
        ImGui::SameLine();
        ImGui::Checkbox("Auto-rebuild", &auto_rebuild);
        if(ImGui::Button("Rebuild Graph"))
            _rebuild_graph_from_plan();
        if (_graphplan_editor->draw() && auto_rebuild)
            _rebuild_graph_from_plan();
        ImGui::TreePop();
    }

    ImGui::End();
}

void audio::_init_graphplan()
{
    log::info("[audio] creating graphplan");
    _graphplan = new graphplan::plan(AUDIO_DOMAIN);

    using NT = audio_graph::node_type;
    auto add = [&](NT t, float x, float y) {
        return _graphplan->add_node_from_type(static_cast<int>(t), x, y);
    };
    auto link = [&](uint64_t out_node, uint64_t in_node) {
        uint64_t lid = _graphplan->get_next_unique_id();
        uint64_t out_pin = _graphplan->nodes.at(out_node).output_pins[0];
        uint64_t in_pin  = _graphplan->nodes.at(in_node).input_pins[0];
        _graphplan->links.insert({lid, graphplan::link_data{lid, in_pin, out_pin}});
    };
    // Fixed output node (always present — maps to audio_graph id=0).
    uint64_t out_id = add(NT::OUTPUT, 650.f, 200.f);

    // BGM chain: Bus Output → Gain → Output
    _bgm_gain_node_id = add(NT::GAIN,       400.f,  80.f);
    uint64_t bgm_bus_id = add(NT::BUS_OUTPUT, 150.f,  80.f);
    _graphplan->nodes.at(bgm_bus_id).properties["bus_id"]  = entt::meta_any{std::string{"bgm"}};
    _graphplan->nodes.at(_bgm_gain_node_id).properties["gain_db"] = entt::meta_any{_bgm_gain_db};
    link(bgm_bus_id, _bgm_gain_node_id);
    link(_bgm_gain_node_id, out_id);

    // SFX chain: Bus Output → Gain → Output
    _sfx_gain_node_id = add(NT::GAIN,       400.f, 320.f);
    uint64_t sfx_bus_id = add(NT::BUS_OUTPUT, 150.f, 320.f);
    _graphplan->nodes.at(sfx_bus_id).properties["bus_id"]  = entt::meta_any{std::string{"sfx"}};
    _graphplan->nodes.at(_sfx_gain_node_id).properties["gain_db"] = entt::meta_any{_sfx_gain_db};
    link(sfx_bus_id, _sfx_gain_node_id);
    link(_sfx_gain_node_id, out_id);

    _rebuild_graph_from_plan();
}

void audio::_rebuild_graph_from_plan()
{
    assert(_graphplan);

    const int OUTPUT_TYPE = static_cast<int>(audio_graph::node_type::OUTPUT);
    const int SOURCE_TYPE = static_cast<int>(audio_graph::node_type::SOURCE);
    const int MIXER_TYPE  = static_cast<int>(audio_graph::node_type::MIXER);
    const int VORBIS_TYPE = static_cast<int>(audio_graph::node_type::VORBIS_SOURCE);
    const int SINE_TYPE   = static_cast<int>(audio_graph::node_type::SINE_SOURCE);
    const int NOISE_TYPE  = static_cast<int>(audio_graph::node_type::NOISE_SOURCE);
    const int REVERB_TYPE     = static_cast<int>(audio_graph::node_type::REVERB);
    const int GAIN_TYPE       = static_cast<int>(audio_graph::node_type::GAIN);
    const int BUS_INPUT_TYPE  = static_cast<int>(audio_graph::node_type::BUS_INPUT);
    const int BUS_OUTPUT_TYPE = static_cast<int>(audio_graph::node_type::BUS_OUTPUT);
    const int COMPRESSOR_TYPE = static_cast<int>(audio_graph::node_type::COMPRESSOR);
    const int EQ5_TYPE        = static_cast<int>(audio_graph::node_type::EQ5);

    auto prop_f = [](const graphplan::node_data& nd, const char* key, float def) -> float {
        auto it = nd.properties.find(key);
        if (it == nd.properties.end()) return def;
        if (const float* v = it->second.try_cast<float>()) return *v;
        return def;
    };
    auto prop_s = [](const graphplan::node_data& nd, const char* key) -> const std::string* {
        auto it = nd.properties.find(key);
        if (it == nd.properties.end()) return nullptr;
        return it->second.try_cast<std::string>();
    };
    auto prop_b = [](const graphplan::node_data& nd, const char* key, bool def) -> bool {
        auto it = nd.properties.find(key);
        if (it == nd.properties.end()) return def;
        if (const bool* v = it->second.try_cast<bool>()) return *v;
        return def;
    };

    // Builds a vorbis producer from the res_id property, optionally wrapped in a looper.
    auto make_vorbis_producer = [&](const graphplan::node_data& nd, uint64_t gp_id)
        -> std::unique_ptr<audio_producer>
    {
        const std::string* res_ptr = prop_s(nd, "res_id");
        const std::string  res_str = res_ptr ? *res_ptr : std::string{};
        _vorbis_res_cache[gp_id] = res_str;
        if (res_str.empty()) return nullptr;

        const char* digits = res_str.c_str();
        if (res_str.size() > 2 && res_str[0] == '0'
            && (res_str[1] == 'x' || res_str[1] == 'X'))
            digits += 2;
        entt::id_type res_id = static_cast<entt::id_type>(std::stoull(digits, nullptr, 16));
        auto vres = rman().get<rvorbis>(res_id);
        if (!vres || !vres->valid)
        {
            log::warn("[audio] vorbis node %llu: resource not found", gp_id);
            return nullptr;
        }
        auto vp = std::make_unique<audio_producer_vorbis>(vres);
        if (!vp->is_valid())
        {
            log::warn("[audio] vorbis node %llu: producer init failed", gp_id);
            return nullptr;
        }
        // Always wrap in a looper: loop_count=0 (infinite) when loop=true, 1 for play-once.
        const size_t loop_count = prop_b(nd, "loop", false) ? 0 : 1;
        return std::make_unique<audio_producer_looper>(
            std::shared_ptr<audio_producer>(std::move(vp)), 0, loop_count);
    };

    // Map graphplan node id → audio_graph node id for edge wiring.
    std::unordered_map<uint64_t, audio_graph::node_id> id_map;

    // Build new graph, reusing cached node instances where possible.
    audio_graph::graph new_graph;
    new_graph.set_spec(_graph.spec());

    // Bus pre-pass: for each unique bus_id, get or create one mixer_node.
    // Both BUS_INPUT and BUS_OUTPUT nodes with the same bus_id will map to it.
    std::unordered_map<std::string, std::shared_ptr<audio_graph::mixer_node>> active_buses;
    for (const auto& [gp_id, nd] : _graphplan->nodes)
    {
        if (nd.type != BUS_INPUT_TYPE && nd.type != BUS_OUTPUT_TYPE) continue;
        const std::string* id_ptr = prop_s(nd, "bus_id");
        const std::string  bus_id = id_ptr ? *id_ptr : std::string{};
        if (bus_id.empty() || active_buses.count(bus_id)) continue;

        // Reuse cached mixer if present, otherwise create a new one.
        std::shared_ptr<audio_graph::mixer_node> bus_mixer;
        auto cache_it = _bus_cache.find(bus_id);
        if (cache_it != _bus_cache.end())
        {
            bus_mixer = cache_it->second;
            log::info("[audio] reused bus mixer '%s' ag=%d", bus_id.c_str(), bus_mixer->id());
        }
        else
        {
            audio_graph::node_id ag_id = _next_ag_id++;
            bus_mixer = std::make_shared<audio_graph::mixer_node>(ag_id);
            _bus_cache[bus_id] = bus_mixer;
            log::info("[audio] created bus mixer '%s' ag=%d", bus_id.c_str(), ag_id);
        }
        new_graph.add_node(bus_mixer);
        active_buses[bus_id] = bus_mixer;
    }
    // Evict stale bus cache entries.
    for (auto it = _bus_cache.begin(); it != _bus_cache.end(); )
        it = active_buses.count(it->first) ? std::next(it) : _bus_cache.erase(it);

    for (const auto& [gp_id, nd] : _graphplan->nodes)
    {
        if (nd.type == OUTPUT_TYPE)
        {
            // Output node always lives in the graph at id=0; keep cache in sync.
            id_map[gp_id] = 0;
            _node_cache[gp_id] = new_graph.nodes().at(0);
            continue;
        }

        if (nd.type == BUS_INPUT_TYPE || nd.type == BUS_OUTPUT_TYPE)
        {
            const std::string* id_ptr = prop_s(nd, "bus_id");
            const std::string  bus_id = id_ptr ? *id_ptr : std::string{};
            auto it = active_buses.find(bus_id);
            if (it != active_buses.end())
                id_map[gp_id] = it->second->id();
            // else: empty bus_id — leave unmapped, edge wiring will silently skip
            continue;
        }

        // Check if we have a cached node of the same type.
        auto cache_it = _node_cache.find(gp_id);
        const bool have_cached = cache_it != _node_cache.end()
                                 && cache_it->second != nullptr;
        // Determine cached type from the node's dynamic type.
        bool type_matches = false;
        if (have_cached)
        {
            if (nd.type == REVERB_TYPE)
                type_matches = dynamic_cast<audio_graph::reverb_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == MIXER_TYPE)
                type_matches = dynamic_cast<audio_graph::mixer_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == GAIN_TYPE)
                type_matches = dynamic_cast<audio_graph::gain_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == COMPRESSOR_TYPE)
                type_matches = dynamic_cast<audio_graph::compressor_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == EQ5_TYPE)
                type_matches = dynamic_cast<audio_graph::eq5_node*>(cache_it->second.get()) != nullptr;
            else // all source variants
                type_matches = dynamic_cast<audio_graph::source_node*>(cache_it->second.get()) != nullptr;
        }

        std::shared_ptr<audio_graph::node> node_ptr;

        if (have_cached && type_matches)
        {
            // Reuse the existing node instance — state (playback position,
            // delay lines, etc.) is preserved.
            node_ptr = cache_it->second;

            // Update mutable params on supported node types.
            if (nd.type == REVERB_TYPE)
            {
                auto* rn = static_cast<audio_graph::reverb_node*>(node_ptr.get());
                rn->set_params(prop_f(nd, "room_size", 0.5f),
                               prop_f(nd, "damping",   0.5f),
                               prop_f(nd, "wet",       0.33f));
            }
            else if (nd.type == VORBIS_TYPE)
            {
                auto* sn = static_cast<audio_graph::source_node*>(node_ptr.get());
                const std::string* res_ptr = prop_s(nd, "res_id");
                const std::string  new_res = res_ptr ? *res_ptr : std::string{};
                if (new_res != _vorbis_res_cache[gp_id])
                {
                    // res_id changed — rebuild the whole producer chain.
                    sn->set_producer(make_vorbis_producer(nd, gp_id));
                }
                else
                {
                    // Only loop flag may have changed — update looper in-place.
                    const size_t loop_count = prop_b(nd, "loop", false) ? 0 : 1;
                    if (auto* lp = dynamic_cast<audio_producer_looper*>(sn->producer()))
                        lp->set_loop_count(loop_count);
                }
            }
            else if (nd.type == SINE_TYPE)
            {
                auto* sn = static_cast<audio_graph::source_node*>(node_ptr.get());
                if (auto* p = dynamic_cast<audio_producer_sine*>(sn->producer()))
                {
                    p->set_frequency(prop_f(nd, "frequency", 440.f));
                    p->set_amplitude(prop_f(nd, "amplitude", 0.5f));
                }
            }
            else if (nd.type == NOISE_TYPE)
            {
                auto* sn = static_cast<audio_graph::source_node*>(node_ptr.get());
                if (auto* p = dynamic_cast<audio_producer_noise*>(sn->producer()))
                    p->set_amplitude(prop_f(nd, "amplitude", 0.5f));
            }
            else if (nd.type == GAIN_TYPE)
            {
                auto* gn = static_cast<audio_graph::gain_node*>(node_ptr.get());
                gn->set_gain_db(prop_f(nd, "gain_db", 0.f));
            }
            else if (nd.type == COMPRESSOR_TYPE)
            {
                auto* cn = static_cast<audio_graph::compressor_node*>(node_ptr.get());
                cn->set_params(prop_f(nd, "threshold_db", -18.f),
                               prop_f(nd, "ratio",        4.f),
                               prop_f(nd, "attack_ms",    10.f),
                               prop_f(nd, "release_ms",   100.f),
                               prop_f(nd, "makeup_db",    0.f));
            }
            else if (nd.type == EQ5_TYPE)
            {
                auto* en = static_cast<audio_graph::eq5_node*>(node_ptr.get());
                char key[32];
                for (int b = 0; b < 5; ++b)
                {
                    snprintf(key, sizeof(key), "band%d_freq",    b); float freq = prop_f(nd, key, 0.f);
                    snprintf(key, sizeof(key), "band%d_gain_db", b); float gain = prop_f(nd, key, 0.f);
                    snprintf(key, sizeof(key), "band%d_q",       b); float q    = prop_f(nd, key, 0.707f);
                    en->set_band(static_cast<size_t>(b), freq, gain, q);
                }
            }

            new_graph.add_node(node_ptr);
            log::info("[audio] reused node gp=%llu ag=%d", gp_id, node_ptr->id());
        }
        else
        {
            // Create a fresh node.
            audio_graph::node_id ag_id = _next_ag_id++;

            if (nd.type == SOURCE_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::source_node>(ag_id);
            }
            else if (nd.type == MIXER_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::mixer_node>(ag_id);
            }
            else if (nd.type == VORBIS_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::source_node>(ag_id, make_vorbis_producer(nd, gp_id));
            }
            else if (nd.type == SINE_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::source_node>(ag_id,
                    std::make_unique<audio_producer_sine>(new_graph.spec(),
                        prop_f(nd, "frequency", 440.f), prop_f(nd, "amplitude", 0.5f)));
            }
            else if (nd.type == NOISE_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::source_node>(ag_id,
                    std::make_unique<audio_producer_noise>(new_graph.spec(),
                        prop_f(nd, "amplitude", 0.5f)));
            }
            else if (nd.type == REVERB_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::reverb_node>(ag_id,
                    prop_f(nd, "room_size", 0.5f),
                    prop_f(nd, "damping",   0.5f),
                    prop_f(nd, "wet",       0.33f));
            }
            else if (nd.type == GAIN_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::gain_node>(ag_id,
                    prop_f(nd, "gain_db", 0.f));
            }
            else if (nd.type == COMPRESSOR_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::compressor_node>(ag_id,
                    prop_f(nd, "threshold_db", -18.f),
                    prop_f(nd, "ratio",        4.f),
                    prop_f(nd, "attack_ms",    10.f),
                    prop_f(nd, "release_ms",   100.f),
                    prop_f(nd, "makeup_db",    0.f));
            }
            else if (nd.type == EQ5_TYPE)
            {
                auto en = std::make_shared<audio_graph::eq5_node>(ag_id);
                char key[32];
                for (int b = 0; b < 5; ++b)
                {
                    snprintf(key, sizeof(key), "band%d_freq",    b); float freq = prop_f(nd, key, 0.f);
                    snprintf(key, sizeof(key), "band%d_gain_db", b); float gain = prop_f(nd, key, 0.f);
                    snprintf(key, sizeof(key), "band%d_q",       b); float q    = prop_f(nd, key, 0.707f);
                    en->set_band(static_cast<size_t>(b), freq, gain, q);
                }
                node_ptr = std::move(en);
            }
            else
            {
                log::warn("[audio] graphplan node %llu has unknown type %d — skipped", gp_id, nd.type);
                continue;
            }

            new_graph.add_node(node_ptr);
            _node_cache[gp_id] = node_ptr;
            log::info("[audio] created node gp=%llu ag=%d type=%d", gp_id, ag_id, nd.type);
        }

        id_map[gp_id] = node_ptr->id();
    }

    // Evict cache entries for graphplan nodes that no longer exist.
    for (auto it = _node_cache.begin(); it != _node_cache.end(); )
    {
        if (!_graphplan->nodes.count(it->first))
        {
            _vorbis_res_cache.erase(it->first);
            it = _node_cache.erase(it);
        }
        else
            ++it;
    }

    // Add edges: for each link, output_pin belongs to the signal source,
    // input_pin belongs to the signal destination.
    for (const auto& [link_id, lk] : _graphplan->links)
    {
        auto src_pin_it = _graphplan->pins.find(lk.output_pin);
        auto dst_pin_it = _graphplan->pins.find(lk.input_pin);
        if (src_pin_it == _graphplan->pins.end() || dst_pin_it == _graphplan->pins.end())
        {
            log::warn("[audio] graphplan link %llu references unknown pin — skipped", link_id);
            continue;
        }

        auto src_it = id_map.find(src_pin_it->second.node_id);
        auto dst_it = id_map.find(dst_pin_it->second.node_id);
        if (src_it == id_map.end() || dst_it == id_map.end())
        {
            log::warn("[audio] graphplan link %llu references unmapped node — skipped", link_id);
            continue;
        }

        new_graph.connect(src_it->second, dst_it->second);
    }

    // Dump full graph state to log.
    {
        const auto& nodes = new_graph.nodes();
        const auto& edges = new_graph.edges();
        log::info("[audio] --- graph rebuilt: %zu nodes, %zu edges ---",
                  nodes.size(), edges.size());
        auto order = new_graph.topological_sort(nullptr);
        for (auto ag_id : order)
        {
            auto* n = nodes.at(ag_id).get();
            // count outgoing edges
            size_t out_edges = edges.count(ag_id) ? edges.at(ag_id).size() : 0;
            // find the graphplan node id for display
            uint64_t gp_id_display = 0;
            for (const auto& [gp, ag] : id_map)
                if (ag == ag_id) { gp_id_display = gp; break; }
            const auto* gp_nd = _graphplan->nodes.count(gp_id_display)
                                 ? &_graphplan->nodes.at(gp_id_display) : nullptr;
            const char* type_name = "?";
            if (gp_nd)
            {
                const int t = gp_nd->type;
                if (t == OUTPUT_TYPE) type_name = "Output";
                else if (t == SOURCE_TYPE) type_name = "Source";
                else if (t == MIXER_TYPE)  type_name = "Mixer";
                else if (t == VORBIS_TYPE) type_name = "Vorbis";
                else if (t == SINE_TYPE)   type_name = "Sine";
                else if (t == NOISE_TYPE)  type_name = "Noise";
                else if (t == REVERB_TYPE) type_name = "Reverb";
            }
            log::info("[audio]   node ag=%d (gp=%llu) type=%s out_edges=%zu",
                      ag_id, gp_id_display, type_name, out_edges);
            if (gp_nd && gp_nd->type == VORBIS_TYPE)
            {
                const std::string* res_ptr  = prop_s(*gp_nd, "res_id");
                const bool*        loop_ptr = [&]() -> const bool* {
                    auto it = gp_nd->properties.find("loop");
                    return it != gp_nd->properties.end() ? it->second.try_cast<bool>() : nullptr;
                }();
                const char* res_str  = res_ptr  ? res_ptr->c_str() : "(none)";
                const char* loop_str = loop_ptr ? (*loop_ptr ? "1" : "0") : "(none)";
                // check whether the source_node actually has a producer
                auto* sn = dynamic_cast<audio_graph::source_node*>(n);
                log::info("[audio]     res_id=%s loop=%s producer=%s",
                          res_str, loop_str, (sn && sn->producer()) ? "ok" : "NULL");
            }
            // incoming edges
            for (const auto& [src, dsts] : edges)
                for (auto dst : dsts)
                    if (dst == ag_id)
                        log::info("[audio]     <- from ag=%d", src);
        }
        log::info("[audio] --- end graph ---");
    }

    SDL_LockMutex(_graph_mtx);
    _graph = std::move(new_graph);
    SDL_UnlockMutex(_graph_mtx);
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