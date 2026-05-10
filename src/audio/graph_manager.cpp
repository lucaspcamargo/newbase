#include <newbase/audio/graph_manager.hpp>
#include <newbase/audio/res/rlpcvocab.hpp>
#include <newbase/audio/vorbis_feedback.hpp>
#include <newbase/audio/visualizer_feedback.hpp>
#include <newbase/audio/producer/looper.hpp>
#include <newbase/audio/producer/vorbis.hpp>
#include <newbase/audio/producer/generators.hpp>
#include <newbase/audio/graph/graph.hpp>
#include <newbase/audio/graph/nodes.hpp>
#include <newbase/graphplan/plan.hpp>
#include <newbase/graphplan/editor.hpp>
#include <newbase/graphplan/domain_registry.hpp>
#include <newbase/res/graphplan.hpp>
#include <newbase/res/loaders.hpp>
#include <newbase/res/writers.hpp>
#include <newbase/yaml/meta_any.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/vorbis.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/log.hpp>
#include <newbase/ui/imgui_icons.hpp>
#ifdef NEWBASE_TALKIE_PCM
#include <newbase/audio/talkie_pcm_vocab.hpp>
#endif
#include <entt/locator/locator.hpp>
#include <entt/meta/meta.hpp>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_node_editor.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <complex>
#include <cmath>

using namespace nb;
using entt::operator""_hs;

// ---------------------------------------------------------------------------
// impl struct — all per-instance state that was previously file-level statics
// ---------------------------------------------------------------------------
struct nb::audio_graph_manager::impl {
    graphplan::plan*   gplan  {nullptr};
    graphplan::editor* editor {nullptr};

    // graphplan node id of the main OUTPUT node (always present after init).
    uint64_t out_node_id {0};
    // Number of managed buses added so far (used for vertical layout).
    int bus_count {0};

    // Persistent node cache: graphplan node id → live audio_graph node instance.
    std::unordered_map<uint64_t, std::shared_ptr<audio_graph::node>> node_cache;
    // Tracks which rvorbis resource each cached vorbis node was built with.
    std::unordered_map<uint64_t, std::shared_ptr<rvorbis>>           vorbis_res_cache;
    // Bus mixer cache: bus_id → shared mixer_node.
    std::unordered_map<std::string, std::shared_ptr<audio_graph::mixer_node>> bus_cache;
    // Monotonically increasing audio_graph node id counter.
    audio_graph::node_id next_ag_id {1};

    std::unordered_map<uint64_t, std::shared_ptr<vorbis_feedback>>      vorbis_fb_cache;
    std::unordered_map<uint64_t, std::shared_ptr<visualizer_feedback>>   vis_fb_cache;
#ifdef NEWBASE_TALKIE_PCM
    std::unordered_map<uint64_t, std::shared_ptr<audio_graph::talkie_pcm_feedback>> talkie_pcm_fb_cache;
#endif
    std::unordered_map<uint64_t, std::shared_ptr<audio_graph::lpc_feedback>> lpc_fb_cache;
    std::unordered_map<uint64_t, std::shared_ptr<rlpcvocab>>                 lpc_vocab_cache;

    // Fan-out buffer cache: graphplan node id → internally-injected fan_out_node.
    // Keyed by (gp_id, output_pin_index) packed as gp_id*16 + pin_index.
    // Currently all nodes have at most one output pin so pin_index is always 0.
    std::unordered_map<uint64_t, std::shared_ptr<audio_graph::fan_out_node>> fan_out_cache;

    // Slot layout: each player occupies a row index.
    // vorbis_node_id → slot index.
    std::unordered_map<uint64_t, int> player_slots;

    // Find the lowest non-negative slot index not currently occupied.
    int alloc_slot() const {
        for (int s = 0; ; ++s) {
            bool taken = false;
            for (const auto& [_, used] : player_slots)
                if (used == s) { taken = true; break; }
            if (!taken) return s;
        }
    }
};

// ---------------------------------------------------------------------------
// Visualizer FFT helper — Cooley-Tukey radix-2 DIT, in-place, n must be power of 2
// ---------------------------------------------------------------------------
static void vis_fft(std::complex<float>* x, size_t n)
{
    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    // Butterfly passes.
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const float angle = -2.f * 3.14159265f / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w(1.f, 0.f);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = x[i + j];
                const auto v = x[i + j + len / 2] * w;
                x[i + j]           = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Audio domain definition — node types, pins, colors, draw functions
// ---------------------------------------------------------------------------
static constexpr int AUDIO_PIN_STREAM = 1;

static const graphplan::domain AUDIO_DOMAIN = []() {
    using namespace graphplan;
    const int OUT = static_cast<int>(audio_graph::node_type::OUTPUT);
    const int SRC = static_cast<int>(audio_graph::node_type::SOURCE);
    const int MIX = static_cast<int>(audio_graph::node_type::MIXER);
    const int VBR = static_cast<int>(audio_graph::node_type::VORBIS_SOURCE);
    const int SIN = static_cast<int>(audio_graph::node_type::SINE_SOURCE);
    const int NOI = static_cast<int>(audio_graph::node_type::NOISE_SOURCE);
    const int PIT = static_cast<int>(audio_graph::node_type::PITCH);
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
                      { prop_def{"res_id", entt::meta_any{entt::id_type{0}}, false,
                                 entt::hashed_string{"rvorbis"}.value()},
                        prop_def{"loop", entt::meta_any{false}} },
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
        node_type_def{static_cast<int>(audio_graph::node_type::VISUALIZER), "Visualizer",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      {}, true, CAT_ROUTING},
        node_type_def{static_cast<int>(audio_graph::node_type::BITCRUSHER), "Bitcrusher",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"bits",       entt::meta_any{8.f}},
                        prop_def{"downsample", entt::meta_any{1.f}} },
                      true, CAT_FX},
        node_type_def{static_cast<int>(audio_graph::node_type::DELAY), "Delay",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"delay_ms",  entt::meta_any{250.f}},
                        prop_def{"feedback",  entt::meta_any{0.4f}},
                        prop_def{"mix",       entt::meta_any{0.5f}} },
                      true, CAT_FX},
        node_type_def{static_cast<int>(audio_graph::node_type::RING_MOD), "Ring Mod",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"carrier_hz", entt::meta_any{200.f}},
                        prop_def{"mix",        entt::meta_any{1.f}} },
                      true, CAT_FX},
        node_type_def{static_cast<int>(audio_graph::node_type::CHORUS), "Chorus",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"rate_hz",  entt::meta_any{0.5f}},
                        prop_def{"depth_ms", entt::meta_any{8.f}},
                        prop_def{"voices",   entt::meta_any{2.f}},
                        prop_def{"mix",      entt::meta_any{0.5f}} },
                      true, CAT_FX},
        node_type_def{static_cast<int>(audio_graph::node_type::WAVESHAPER), "Waveshaper",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"drive", entt::meta_any{5.f}},
                        prop_def{"shape", entt::meta_any{0.f}},
                        prop_def{"mix",   entt::meta_any{1.f}} },
                      true, CAT_FX},
        node_type_def{static_cast<int>(audio_graph::node_type::PHASER), "Phaser",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"rate_hz",  entt::meta_any{0.5f}},
                        prop_def{"depth",    entt::meta_any{0.8f}},
                        prop_def{"stages",   entt::meta_any{4.f}},
                        prop_def{"feedback", entt::meta_any{0.5f}},
                        prop_def{"mix",      entt::meta_any{0.5f}} },
                      true, CAT_FX},
        node_type_def{PIT, "Pitch",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"pitch_ratio", entt::meta_any{1.f}} },
                      true, CAT_FX},
#ifdef NEWBASE_TALKIE_PCM
        node_type_def{static_cast<int>(audio_graph::node_type::TALKIE_PCM_SOURCE), "TalkiePCM",
                      {}, {AUDIO_PIN_STREAM},
                      { prop_def{"volume", entt::meta_any{1.f}} },
                      true, CAT_SOURCE},
#endif
        node_type_def{static_cast<int>(audio_graph::node_type::LPC_SOURCE), "LPC Speech",
                      {}, {AUDIO_PIN_STREAM},
                      { prop_def{"volume",       entt::meta_any{1.f}},
                        prop_def{"vocab_res_id", entt::meta_any{entt::id_type{0}}, false,
                                 entt::hashed_string{"rlpcvocab"}.value()} },
                      true, CAT_SOURCE},
        node_type_def{static_cast<int>(audio_graph::node_type::GROUP), "Group",
                      {AUDIO_PIN_STREAM}, {AUDIO_PIN_STREAM},
                      { prop_def{"res_id", entt::meta_any{entt::id_type{0}}, false,
                                 entt::hashed_string{"rgraphplan"}.value()} },
                      true, CAT_ROUTING},
        node_type_def{static_cast<int>(audio_graph::node_type::GROUP_INPUT),  "Group Input",
                      {}, {AUDIO_PIN_STREAM}, {}, false, CAT_ROUTING},
        node_type_def{static_cast<int>(audio_graph::node_type::GROUP_OUTPUT), "Group Output",
                      {AUDIO_PIN_STREAM}, {}, {}, false, CAT_ROUTING},
    };

    auto set_color = [&](audio_graph::node_type nt, ImVec4 color) {
        auto it = std::find_if(d.node_types.begin(), d.node_types.end(),
            [nt](const graphplan::node_type_def& t){ return t.type_id == static_cast<int>(nt); });
        if (it != d.node_types.end())
        {
            it->header_color[0] = color.x; it->header_color[1] = color.y;
            it->header_color[2] = color.z; it->header_color[3] = color.w;
        }
    };

    static constexpr float A = 0.25f;
    static constexpr ImVec4 CORE_COLOR  (0.0f, 0.0f, 1.0f, A);
    static constexpr ImVec4 BUS_COLOR   (0.0f, 1.0f, 1.0f, A);
    static constexpr ImVec4 FX_COLOR    (1.0f, 0.0f, 1.0f, A);
    static constexpr ImVec4 SOURCE_COLOR(1.0f, 1.0f, 0.0f, A);

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
    set_color(audio_graph::node_type::VISUALIZER,    CORE_COLOR);
    set_color(audio_graph::node_type::BITCRUSHER,    FX_COLOR);
    set_color(audio_graph::node_type::DELAY,         FX_COLOR);
    set_color(audio_graph::node_type::RING_MOD,      FX_COLOR);
    set_color(audio_graph::node_type::CHORUS,        FX_COLOR);
    set_color(audio_graph::node_type::WAVESHAPER,    FX_COLOR);
    set_color(audio_graph::node_type::PHASER,        FX_COLOR);
    set_color(audio_graph::node_type::PITCH,         FX_COLOR);
#ifdef NEWBASE_TALKIE_PCM
    set_color(audio_graph::node_type::TALKIE_PCM_SOURCE, SOURCE_COLOR);
#endif
    set_color(audio_graph::node_type::LPC_SOURCE,   SOURCE_COLOR);
    set_color(audio_graph::node_type::GROUP,        CORE_COLOR);
    set_color(audio_graph::node_type::GROUP_INPUT,  BUS_COLOR);
    set_color(audio_graph::node_type::GROUP_OUTPUT, BUS_COLOR);

    // Vorbis custom draw.
    auto vbr_it = std::find_if(d.node_types.begin(), d.node_types.end(),
        [](const graphplan::node_type_def& t){ return t.type_id == static_cast<int>(audio_graph::node_type::VORBIS_SOURCE); });
    if (vbr_it != d.node_types.end())
    {
        vbr_it->draw_fn = [](graphplan::node_data& nd) -> bool {
            auto* fb = static_cast<vorbis_feedback*>(nd.user_data.get());
            if (!fb) { ImGui::TextDisabled("(no file)"); return false; }

            const size_t curr  = fb->curr_frame  .load(std::memory_order_relaxed);
            const size_t total = fb->total_frames .load(std::memory_order_relaxed);
            const size_t plays = fb->play_count   .load(std::memory_order_relaxed);
            const size_t loops = fb->loop_count   .load(std::memory_order_relaxed);

            float pos = (total > 0) ? static_cast<float>(curr) / static_cast<float>(total) : 0.f;
            ImGui::SetNextItemWidth(160.f);
            if (ImGui::SliderFloat("##pos", &pos, 0.f, 1.f, ""))
            {
                fb->cmd_seek_frame.store(static_cast<size_t>(pos * static_cast<float>(total)),
                                         std::memory_order_relaxed);
                fb->cmd_seek.store(true, std::memory_order_release);
            }
            if (ImGui::IsItemHovered() && total > 0)
                ImGui::SetTooltip("frame %zu / %zu", curr, total);

            if (ImGui::Button(ICON_FK_FAST_BACKWARD " Rewind"))
                fb->cmd_rewind.store(true, std::memory_order_release);

            ImGui::Text("curr_frame:   %zu", curr);
            ImGui::Text("total_frames: %zu", total);
            ImGui::Text("play_count:   %zu", plays);
            if (loops == 0) ImGui::Text("loop_count:   inf (0)");
            else            ImGui::Text("loop_count:   %zu", loops);

            return false;
        };
    }

    // EQ5 custom draw.
    auto eq5_it = std::find_if(d.node_types.begin(), d.node_types.end(),
        [](const graphplan::node_type_def& t){ return t.type_id == static_cast<int>(audio_graph::node_type::EQ5); });
    if (eq5_it != d.node_types.end())
    {
        eq5_it->draw_fn = [](graphplan::node_data& nd) -> bool {
            static constexpr float SLIDER_W = 36.f, SLIDER_H = 100.f;
            static constexpr float FREQ_W   = 56.f, COL_W    = 60.f;
            static constexpr float GAIN_MIN = -24.f, GAIN_MAX = 24.f;
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
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (COL_W - SLIDER_W) * 0.5f);
                float gain = *gain_p;
                if (ImGui::VSliderFloat("##g", ImVec2(SLIDER_W, SLIDER_H), &gain, GAIN_MIN, GAIN_MAX, ""))
                    { nd.properties[gain_key] = entt::meta_any{gain}; changed = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%.1f dB", gain);
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

    // Visualizer custom draw.
    auto vis_it = std::find_if(d.node_types.begin(), d.node_types.end(),
        [](const graphplan::node_type_def& t){ return t.type_id == static_cast<int>(audio_graph::node_type::VISUALIZER); });
    if (vis_it != d.node_types.end())
    {
        vis_it->draw_fn = [](graphplan::node_data& nd) -> bool {
            auto* fb = static_cast<visualizer_feedback*>(nd.user_data.get());
            if (!fb) { ImGui::TextDisabled("(no signal)"); return false; }
            auto* rs = entt::locator<renderer_service*>::has_value()
                       ? entt::locator<renderer_service*>::value() : nullptr;
            if (!rs) { ImGui::TextDisabled("(no renderer)"); return false; }

            // Mode toggle
            {
                const char* label = fb->spectrum_mode ? "FFT" : "Wave";
                if (ImGui::SmallButton(label))
                    fb->spectrum_mode = !fb->spectrum_mode;
                ImGui::SameLine();
                ImGui::TextDisabled("%u Hz", fb->sample_rate);
            }

            if (!fb->surface)
            {
                fb->surface = SDL_CreateSurface(fb->tex_w, fb->tex_h, SDL_PIXELFORMAT_RGBA32);
                if (!fb->surface) return false;
                SDL_ClearSurface(fb->surface, 0.f, 0.f, 0.f, 1.f);
            }
            if (!fb->texture)
            {
                fb->texture = rs->create_texture(fb->tex_w, fb->tex_h);
                if (!fb->texture) return false;
            }

            size_t n = fb->snapshot();
            if (n > 0)
            {
                const int W = fb->tex_w, H = fb->tex_h;
                SDL_ClearSurface(fb->surface, 0.05f, 0.05f, 0.05f, 1.f);
                Uint32* pixels    = static_cast<Uint32*>(fb->surface->pixels);
                const int pitch_px = fb->surface->pitch / 4;
                const auto* fmt   = SDL_GetPixelFormatDetails(fb->surface->format);

                if (!fb->spectrum_mode)
                {
                    // ---- waveform ----
                    const float mid_y = H * 0.5f;
                    const Uint32 wave_col   = SDL_MapRGBA(fmt, nullptr, 60,  220, 80,  255);
                    const Uint32 center_col = SDL_MapRGBA(fmt, nullptr, 40,  80,  40,  255);
                    const int cy = static_cast<int>(mid_y);
                    for (int x = 0; x < W; ++x) pixels[cy * pitch_px + x] = center_col;
                    const size_t start = n >= static_cast<size_t>(W) ? n - static_cast<size_t>(W) : 0;
                    const size_t count = n - start;
                    for (size_t i = 0; i < count; ++i)
                    {
                        float s = fb->read_buf[start + i];
                        s = s < -1.f ? -1.f : (s > 1.f ? 1.f : s);
                        int y  = static_cast<int>(mid_y - s * mid_y);
                        y = y < 0 ? 0 : (y >= H ? H - 1 : y);
                        int xi = static_cast<int>(i);
                        int y0 = cy, y1 = y;
                        if (y1 < y0) { int tmp = y0; y0 = y1; y1 = tmp; }
                        for (int yy = y0; yy <= y1; ++yy) pixels[yy * pitch_px + xi] = wave_col;
                    }
                }
                else
                {
                    // ---- spectrum (FFT) ----
                    constexpr size_t FFT_N = 1024;
                    static std::complex<float> fft_buf[FFT_N];

                    // Fill FFT input with the last FFT_N samples; apply Hann window.
                    const size_t avail = n < FFT_N ? n : FFT_N;
                    const size_t src_start = n - avail;
                    for (size_t i = 0; i < FFT_N; ++i)
                    {
                        float sample = (i < avail) ? fb->read_buf[src_start + i] : 0.f;
                        const float hann = 0.5f * (1.f - std::cos(2.f * 3.14159265f * static_cast<float>(i) / static_cast<float>(FFT_N - 1)));
                        fft_buf[i] = std::complex<float>(sample * hann, 0.f);
                    }
                    vis_fft(fft_buf, FFT_N);

                    // Log-frequency mapping: 20 Hz .. min(sr/2, 20 000 Hz)
                    const float sr      = static_cast<float>(fb->sample_rate > 0 ? fb->sample_rate : 44100);
                    const float f_lo    = 20.f;
                    const float f_hi    = std::min(sr * 0.5f, 20000.f);
                    const float log_lo  = std::log2(f_lo);
                    const float log_hi  = std::log2(f_hi);
                    const float log_rng = log_hi - log_lo;

                    // Draw octave grid lines (faint)
                    const Uint32 grid_col = SDL_MapRGBA(fmt, nullptr, 50, 50, 60, 255);
                    for (float oct = std::ceil(log_lo); oct <= log_hi; oct += 1.f)
                    {
                        int gx = static_cast<int>((oct - log_lo) / log_rng * static_cast<float>(W - 1));
                        if (gx >= 0 && gx < W)
                            for (int y = 0; y < H; ++y)
                                pixels[y * pitch_px + gx] = grid_col;
                    }

                    // Draw magnitude bars
                    for (int xi = 0; xi < W; ++xi)
                    {
                        // Frequency at this pixel
                        const float t = static_cast<float>(xi) / static_cast<float>(W - 1);
                        const float freq = std::exp2(log_lo + t * log_rng);

                        // Nearest FFT bin
                        const size_t bin = static_cast<size_t>(freq / sr * static_cast<float>(FFT_N));
                        const size_t bin_clamped = bin < FFT_N / 2 ? bin : FFT_N / 2 - 1;
                        const float mag = std::abs(fft_buf[bin_clamped]) * 2.f / static_cast<float>(FFT_N);

                        // Convert to dB, normalize to [0,1] in the range [-80, 0] dB
                        const float db  = mag > 1e-7f ? 20.f * std::log10(mag) : -80.f;
                        const float bar = (db + 80.f) / 80.f; // 0 = silent, 1 = 0 dBFS
                        const float bf  = bar < 0.f ? 0.f : (bar > 1.f ? 1.f : bar);
                        const int bar_h = static_cast<int>(bf * static_cast<float>(H));

                        // Color: green (low) → yellow (mid) → red (high)
                        Uint8 r, g, b;
                        if (bf < 0.5f) { r = static_cast<Uint8>(bf * 2.f * 255); g = 220; b = 40; }
                        else           { r = 255; g = static_cast<Uint8>((1.f - (bf - 0.5f) * 2.f) * 220); b = 40; }

                        const Uint32 bar_col = SDL_MapRGBA(fmt, nullptr, r, g, b, 255);
                        for (int y = H - bar_h; y < H; ++y)
                            pixels[y * pitch_px + xi] = bar_col;
                    }
                }

                rs->update_texture(fb->texture, fb->surface->pixels, fb->surface->pitch);
            }

            ImGui::Image((ImTextureID)fb->texture,
                         ImVec2(static_cast<float>(fb->tex_w), static_cast<float>(fb->tex_h)));
            return false;
        };
    }

#ifdef NEWBASE_TALKIE_PCM
    // TalkiePCM custom draw.
    auto talkie_it = std::find_if(d.node_types.begin(), d.node_types.end(),
        [](const graphplan::node_type_def& t){
            return t.type_id == static_cast<int>(audio_graph::node_type::TALKIE_PCM_SOURCE);
        });
    if (talkie_it != d.node_types.end())
    {
        talkie_it->draw_fn = [](graphplan::node_data& nd) -> bool
        {
            auto* fb = static_cast<audio_graph::talkie_pcm_feedback*>(nd.user_data.get());
            if (!fb) { ImGui::TextDisabled("(not initialized)"); return false; }
            auto node = fb->node_wptr.lock();
            if (!node) { ImGui::TextDisabled("(no live node)"); return false; }

            // --- Word selector ---
            size_t word_count = 0;
            const nb::talkie_vocab_entry* words = nb::talkie_vocab_all(word_count);
            if (fb->ui_word_idx < 0 || fb->ui_word_idx >= static_cast<int>(word_count))
                fb->ui_word_idx = 0;

            ImGui::TextUnformatted(ICON_FK_MICROPHONE " Say word");
            ImGui::SetNextItemWidth(140.f);
            ImGui::Combo("##word", &fb->ui_word_idx,
                [](void* data, int idx, const char** out) -> bool {
                    *out = static_cast<const nb::talkie_vocab_entry*>(data)[idx].word;
                    return true;
                },
                const_cast<void*>(static_cast<const void*>(words)),
                static_cast<int>(word_count));

            const nb::talkie_vocab_entry& entry = words[fb->ui_word_idx];
            if (fb->ui_variant_idx < 0 || fb->ui_variant_idx >= static_cast<int>(entry.variant_count))
                fb->ui_variant_idx = 0;

            if (entry.variant_count > 1)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50.f);
                ImGui::Combo("##var", &fb->ui_variant_idx,
                    [](void* data, int idx, const char** out) -> bool {
                        *out = static_cast<const char* const*>(data)[idx];
                        return true;
                    },
                    const_cast<void*>(static_cast<const void*>(entry.variant_names)),
                    static_cast<int>(entry.variant_count));
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FK_PLAY "##sayword"))
                node->say(entry.variants[fb->ui_variant_idx]);

            // --- Say number ---
            ImGui::TextUnformatted(ICON_FK_MICROPHONE " Say number");
            ImGui::SetNextItemWidth(80.f);
            ImGui::InputInt("##num", &fb->ui_number);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FK_PLAY "##saynum"))
                node->say_number(static_cast<long>(fb->ui_number));

            // --- Silence / pause ---
            ImGui::SetNextItemWidth(80.f);
            ImGui::InputInt("ms##sil", &fb->ui_silence_ms);
            if (fb->ui_silence_ms < 0) fb->ui_silence_ms = 0;
            ImGui::SameLine();
            if (ImGui::Button(ICON_FK_VOLUME_OFF " Silence"))
                node->silence_ms(static_cast<uint16_t>(fb->ui_silence_ms));
            ImGui::SameLine();
            if (ImGui::Button("Pause"))
                node->say_pause();

            // --- Phrase ---
            ImGui::TextUnformatted(ICON_FK_MICROPHONE " Say phrase");
            ImGui::SetNextItemWidth(200.f);
            ImGui::InputText("##phrase", fb->ui_phrase, sizeof(fb->ui_phrase));
            ImGui::SameLine();
            if (ImGui::Button(ICON_FK_PLAY "##sayphrase"))
                node->say_phrase(fb->ui_phrase);

            return false;
        };
    }
#endif

    // LPC Speech custom draw.
    auto lpc_it = std::find_if(d.node_types.begin(), d.node_types.end(),
        [](const graphplan::node_type_def& t){
            return t.type_id == static_cast<int>(audio_graph::node_type::LPC_SOURCE);
        });
    if (lpc_it != d.node_types.end())
    {
        lpc_it->draw_fn = [](graphplan::node_data& nd) -> bool
        {
            auto* fb = static_cast<audio_graph::lpc_feedback*>(nd.user_data.get());
            if (!fb) { ImGui::TextDisabled("(not initialized)"); return false; }
            auto node = fb->node_wptr.lock();
            if (!node) { ImGui::TextDisabled("(no live node)"); return false; }

            const nb::rlpcvocab* vocab = fb->vocab && fb->vocab->valid ? fb->vocab.get() : nullptr;
            const size_t word_count = vocab ? vocab->words.size() : 0;

            // --- Word selector ---
            if (word_count == 0)
            {
                ImGui::TextDisabled("(no vocabulary loaded)");
            }
            else
            {
                if (fb->ui_word_idx < 0 || fb->ui_word_idx >= static_cast<int>(word_count))
                    fb->ui_word_idx = 0;

                ImGui::TextUnformatted(ICON_FK_MICROPHONE " Say word");
                ImGui::SetNextItemWidth(140.f);
                ImGui::Combo("##lpcword", &fb->ui_word_idx,
                    [](void* data, int idx, const char** out) -> bool {
                        *out = static_cast<const nb::rlpcvocab*>(data)->words[idx].name.c_str();
                        return true;
                    },
                    const_cast<void*>(static_cast<const void*>(vocab)),
                    static_cast<int>(word_count));

                const nb::rlpcvocab_word& entry = vocab->words[fb->ui_word_idx];
                const size_t var_count = entry.variants.size();
                if (fb->ui_variant_idx < 0 || fb->ui_variant_idx >= static_cast<int>(var_count))
                    fb->ui_variant_idx = 0;

                if (var_count > 1)
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(50.f);
                    ImGui::Combo("##lpcvar", &fb->ui_variant_idx,
                        [](void* data, int idx, const char** out) -> bool {
                            *out = static_cast<const nb::rlpcvocab_word*>(data)
                                       ->variants[idx].name.c_str();
                            return true;
                        },
                        const_cast<void*>(static_cast<const void*>(&entry)),
                        static_cast<int>(var_count));
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FK_PLAY "##lpcsayword"))
                {
                    const auto& v = entry.variants[fb->ui_variant_idx];
                    node->say(v.data.data(), v.data.size());
                }
            }

            // --- Silence ---
            ImGui::SetNextItemWidth(80.f);
            ImGui::InputInt("ms##lpcsil", &fb->ui_silence_ms);
            if (fb->ui_silence_ms < 0) fb->ui_silence_ms = 0;
            ImGui::SameLine();
            if (ImGui::Button(ICON_FK_VOLUME_OFF " Silence"))
                node->silence_ms(static_cast<uint16_t>(fb->ui_silence_ms));

            // --- Phrase ---
            ImGui::BeginDisabled(word_count == 0);
            ImGui::TextUnformatted(ICON_FK_MICROPHONE " Say phrase");
            ImGui::SetNextItemWidth(200.f);
            ImGui::InputText("##lpcphrase", fb->ui_phrase, sizeof(fb->ui_phrase));
            ImGui::SameLine();
            if (ImGui::Button(ICON_FK_PLAY "##lpcsayphrase"))
                node->say_phrase(fb->ui_phrase);
            ImGui::EndDisabled();

            return false;
        };
    }

    return d;
}();

// ---------------------------------------------------------------------------
// audio_graph_manager implementation
// ---------------------------------------------------------------------------

audio_graph_manager::audio_graph_manager()  : _d(new impl()) {}
audio_graph_manager::~audio_graph_manager() { shutdown(); delete _d; }

graphplan::plan* audio_graph_manager::plan() const { return _d->gplan; }

void audio_graph_manager::init(audio_spec spec)
{
    graphplan::register_domain(AUDIO_DOMAIN);

    log::verb("[audio] creating graphplan");
    _d->gplan = new graphplan::plan(AUDIO_DOMAIN);

    using NT = audio_graph::node_type;
    _d->out_node_id = _d->gplan->add_node_from_type(static_cast<int>(NT::OUTPUT), 650.f, 200.f);

    (void)spec;
}

void audio_graph_manager::_link(uint64_t from_node, uint64_t to_node)
{
    uint64_t lid     = _d->gplan->get_next_unique_id();
    uint64_t out_pin = _d->gplan->nodes.at(from_node).output_pins[0];
    uint64_t in_pin  = _d->gplan->nodes.at(to_node).input_pins[0];
    _d->gplan->links.insert({lid, graphplan::link_data{lid, in_pin, out_pin}});
}

uint64_t audio_graph_manager::add_managed_bus(const std::string& name, float gain_db)
{
    assert(_d->gplan);
    using NT = audio_graph::node_type;

    float y = 80.f + static_cast<float>(_d->bus_count) * 240.f;
    ++_d->bus_count;

    uint64_t bus_out_id = _d->gplan->add_node_from_type(static_cast<int>(NT::BUS_OUTPUT), 150.f, y);
    uint64_t gain_id    = _d->gplan->add_node_from_type(static_cast<int>(NT::GAIN),       400.f, y);
    _d->gplan->nodes.at(bus_out_id).properties["bus_id"]  = entt::meta_any{name};
    _d->gplan->nodes.at(gain_id).properties["gain_db"]    = entt::meta_any{gain_db};
    _link(bus_out_id, gain_id);
    _link(gain_id, _d->out_node_id);

    log::verb("[audio] added managed bus '%s' gain_node=%llu", name.c_str(), gain_id);
    return gain_id;
}

audio_graph_manager::player_nodes audio_graph_manager::add_player(
    const std::string& bus_name, entt::id_type res_id, bool loop)
{
    assert(_d->gplan);
    using NT = audio_graph::node_type;

    // Slot-based layout: players are arranged in rows to the right of the OUTPUT node.
    static constexpr float ROW_BASE_Y  =   80.f;
    static constexpr float ROW_STEP_Y  =  275.f;
    static constexpr float VORBIS_X    =  900.f;
    static constexpr float PITCH_X     = 1100.f;
    static constexpr float GAIN_X      = 1300.f;
    static constexpr float BUS_INPUT_X = 1500.f;

    int   slot = _d->alloc_slot();
    float y    = ROW_BASE_Y + static_cast<float>(slot) * ROW_STEP_Y;

    uint64_t vorbis_id = _d->gplan->add_node_from_type(static_cast<int>(NT::VORBIS_SOURCE), VORBIS_X,    y);
    uint64_t pitch_id  = _d->gplan->add_node_from_type(static_cast<int>(NT::PITCH),          PITCH_X,    y);
    uint64_t gain_id   = _d->gplan->add_node_from_type(static_cast<int>(NT::GAIN),            GAIN_X,     y);
    uint64_t bus_in_id = _d->gplan->add_node_from_type(static_cast<int>(NT::BUS_INPUT),       BUS_INPUT_X, y);
    _d->gplan->nodes.at(vorbis_id).properties["res_id"]      = entt::meta_any{res_id};
    _d->gplan->nodes.at(vorbis_id).properties["loop"]        = entt::meta_any{loop};
    _d->gplan->nodes.at(pitch_id).properties["pitch_ratio"]  = entt::meta_any{1.f};
    _d->gplan->nodes.at(gain_id).properties["gain_db"]       = entt::meta_any{0.f};
    _d->gplan->nodes.at(bus_in_id).properties["bus_id"]      = entt::meta_any{bus_name};
    _link(vorbis_id, pitch_id);
    _link(pitch_id,  gain_id);
    _link(gain_id,   bus_in_id);

    _d->player_slots[vorbis_id] = slot;

    log::verb("[audio] added player slot=%d bus='%s' vorbis=%llu pitch=%llu gain=%llu bus_in=%llu",
              slot, bus_name.c_str(), vorbis_id, pitch_id, gain_id, bus_in_id);
    return {vorbis_id, pitch_id, gain_id, bus_in_id};
}

std::shared_ptr<vorbis_feedback> audio_graph_manager::get_player_feedback(const player_nodes& pn) const
{
    auto it = _d->vorbis_fb_cache.find(pn.vorbis_node_id);
    if (it != _d->vorbis_fb_cache.end()) return it->second;
    return nullptr;
}

void audio_graph_manager::remove_player(const player_nodes& pn)
{
    if (!_d->gplan) return;

    for (uint64_t node_id : {pn.vorbis_node_id, pn.pitch_node_id, pn.gain_node_id, pn.bus_input_node_id})
    {
        auto nit = _d->gplan->nodes.find(node_id);
        if (nit == _d->gplan->nodes.end()) continue;

        const auto& nd = nit->second;
        std::unordered_set<uint64_t> pins;
        pins.insert(nd.input_pins.begin(),  nd.input_pins.end());
        pins.insert(nd.output_pins.begin(), nd.output_pins.end());

        for (auto lit = _d->gplan->links.begin(); lit != _d->gplan->links.end(); )
        {
            if (pins.count(lit->second.input_pin) || pins.count(lit->second.output_pin))
                lit = _d->gplan->links.erase(lit);
            else
                ++lit;
        }
        for (uint64_t pid : pins) _d->gplan->pins.erase(pid);
        _d->gplan->nodes.erase(nit);
    }

    _d->node_cache.erase(pn.vorbis_node_id);
    _d->vorbis_res_cache.erase(pn.vorbis_node_id);
    _d->vorbis_fb_cache.erase(pn.vorbis_node_id);
    _d->player_slots.erase(pn.vorbis_node_id);
    _d->node_cache.erase(pn.pitch_node_id);
    _d->node_cache.erase(pn.gain_node_id);
    _d->node_cache.erase(pn.bus_input_node_id);

    log::verb("[audio] removed player vorbis=%llu pitch=%llu gain=%llu bus_in=%llu",
              pn.vorbis_node_id, pn.pitch_node_id, pn.gain_node_id, pn.bus_input_node_id);
}

void audio_graph_manager::shutdown()
{
    _d->node_cache.clear();
    _d->fan_out_cache.clear();
    _d->vorbis_res_cache.clear();
    _d->vorbis_fb_cache.clear();

    if (auto* rs = entt::locator<renderer_service*>::has_value()
                   ? entt::locator<renderer_service*>::value() : nullptr)
    {
        for (auto& [id, fb] : _d->vis_fb_cache)
            if (fb && fb->texture) { rs->destroy_texture(fb->texture); fb->texture = nullptr; }
    }
    _d->vis_fb_cache.clear();
    _d->bus_cache.clear();

    if (_d->editor) { delete _d->editor; _d->editor = nullptr; }
    if (_d->gplan)  { delete _d->gplan;  _d->gplan  = nullptr; }
}

void audio_graph_manager::apply_gain_db(uint64_t node_id, float db)
{
    if (!node_id || !_d->gplan) return;
    _d->gplan->nodes.at(node_id).properties["gain_db"] = entt::meta_any{db};
    auto it = _d->node_cache.find(node_id);
    if (it != _d->node_cache.end())
        if (auto* gn = dynamic_cast<audio_graph::gain_node*>(it->second.get()))
            gn->set_gain_db(db);
}

void audio_graph_manager::apply_pitch_ratio(uint64_t node_id, float ratio)
{
    if (!node_id || !_d->gplan) return;
    _d->gplan->nodes.at(node_id).properties["pitch_ratio"] = entt::meta_any{ratio};
    auto it = _d->node_cache.find(node_id);
    if (it != _d->node_cache.end())
        if (auto* pn = dynamic_cast<audio_graph::pitch_node*>(it->second.get()))
            pn->set_pitch_ratio(ratio);
}

void audio_graph_manager::_reset_plan_caches(impl* d)
{
    d->node_cache.clear();
    d->fan_out_cache.clear();
    d->vorbis_res_cache.clear();
    d->vorbis_fb_cache.clear();
#ifdef NEWBASE_TALKIE_PCM
    d->talkie_pcm_fb_cache.clear();
#endif
    d->lpc_fb_cache.clear();
    d->lpc_vocab_cache.clear();
    if (auto* rs = entt::locator<renderer_service*>::has_value()
                   ? entt::locator<renderer_service*>::value() : nullptr)
    {
        for (auto& [id, fb] : d->vis_fb_cache)
            if (fb && fb->texture) { rs->destroy_texture(fb->texture); fb->texture = nullptr; }
    }
    d->vis_fb_cache.clear();
    d->bus_cache.clear();
    d->player_slots.clear();
    d->bus_count = 0;
}

void audio_graph_manager::_apply_rgraphplan(impl* d, const rgraphplan& gp)
{
    _reset_plan_caches(d);

    if (d->editor) { delete d->editor; d->editor = nullptr; }

    d->gplan->nodes.clear();
    d->gplan->pins.clear();
    d->gplan->links.clear();
    d->out_node_id = 0;

    // Map from saved node id → newly allocated gp node id.
    std::unordered_map<uint64_t, uint64_t> id_remap;

    const int OUTPUT_TYPE = static_cast<int>(audio_graph::node_type::OUTPUT);

    for (const auto& nd : gp.nodes)
    {
        const auto* tdef = d->gplan->dom().find_type_by_name(nd.type_name.c_str());
        if (!tdef)
        {
            log::warn("[audio] _apply_rgraphplan: unknown type '%s' — skipped", nd.type_name.c_str());
            continue;
        }
        uint64_t new_id = d->gplan->add_node_from_type(tdef->type_id, nd.pos_x, nd.pos_y);
        id_remap[nd.id] = new_id;

        auto& new_nd = d->gplan->nodes.at(new_id);
        for (const auto& [pname, pval] : nd.properties)
            new_nd.properties[pname] = pval;

        if (tdef->type_id == OUTPUT_TYPE)
            d->out_node_id = new_id;
    }

    for (const auto& ld : gp.links)
    {
        auto from_it = id_remap.find(ld.from_node);
        auto to_it   = id_remap.find(ld.to_node);
        if (from_it == id_remap.end() || to_it == id_remap.end()) continue;

        auto& from_nd = d->gplan->nodes.at(from_it->second);
        auto& to_nd   = d->gplan->nodes.at(to_it->second);

        if (ld.from_pin < 0 || ld.from_pin >= static_cast<int>(from_nd.output_pins.size())) continue;
        if (ld.to_pin   < 0 || ld.to_pin   >= static_cast<int>(to_nd.input_pins.size()))   continue;

        uint64_t lid     = d->gplan->get_next_unique_id();
        uint64_t out_pin = from_nd.output_pins[ld.from_pin];
        uint64_t in_pin  = to_nd.input_pins[ld.to_pin];
        d->gplan->links.insert({lid, graphplan::link_data{lid, in_pin, out_pin}});
    }

    log::info("[audio] applied graphplan: %zu nodes, %zu links",
              d->gplan->nodes.size(), d->gplan->links.size());
}

void audio_graph_manager::_load_plan_from_file(impl* d, const char* path)
{
    auto gp = rloader_graphplan::from_path(path);
    if (!gp)
    {
        log::error("[audio] failed to load graphplan from '%s'", path);
        return;
    }
    _apply_rgraphplan(d, *gp);
}

bool audio_graph_manager::draw_editor()
{
    static char s_path[512] = {};

    ImGui::SetNextItemWidth(320.f);
    ImGui::InputText("##path", s_path, sizeof(s_path));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FK_FLOPPY_O " Save"))
        write_graphplan_plan(*_d->gplan, s_path);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FK_FOLDER_OPEN_O " Load"))
        _load_plan_from_file(_d, s_path);
    ImGui::Separator();

    if (!_d->editor)
        _d->editor = new graphplan::editor(*_d->gplan);
    return _d->editor->draw();
}

// ---------------------------------------------------------------------------
// Group subgraph expansion helper
// ---------------------------------------------------------------------------

static constexpr int MAX_GROUP_DEPTH = 8;

audio_graph_manager::group_expand_result audio_graph_manager::_expand_group_impl(
    impl* d,
    audio_graph::graph& new_graph,
    const rgraphplan& subgp,
    int depth)
{
    audio_graph_manager::group_expand_result result;
    if (depth > MAX_GROUP_DEPTH)
    {
        log::error("[audio] group nesting depth limit reached (%d)", depth);
        return result;
    }

    const auto* dom = graphplan::find_domain("audio_graph");
    if (!dom)
    {
        log::error("[audio] audio_graph domain not registered during group expansion");
        return result;
    }

    using NT = audio_graph::node_type;
    const int GRPI_TYPE  = static_cast<int>(NT::GROUP_INPUT);
    const int GRPO_TYPE  = static_cast<int>(NT::GROUP_OUTPUT);
    const int GROUP_TYPE = static_cast<int>(NT::GROUP);

    auto pf = [](const rgraphplan::node_desc& nd, const char* k, float def) -> float {
        auto it = nd.properties.find(k);
        if (it == nd.properties.end()) return def;
        if (const float* v = it->second.try_cast<float>()) return *v;
        return def;
    };
    auto pb = [](const rgraphplan::node_desc& nd, const char* k, bool def) -> bool {
        auto it = nd.properties.find(k);
        if (it == nd.properties.end()) return def;
        if (const bool* v = it->second.try_cast<bool>()) return *v;
        return def;
    };
    auto pid = [](const rgraphplan::node_desc& nd, const char* k) -> entt::id_type {
        auto it = nd.properties.find(k);
        if (it == nd.properties.end()) return 0;
        if (const entt::id_type* v = it->second.try_cast<entt::id_type>()) return *v;
        return 0;
    };

    std::unordered_map<uint64_t, audio_graph::node_id> sub_id_map;
    std::unordered_map<uint64_t, audio_graph::node_id> sub_group_input_map;

    for (const auto& nd : subgp.nodes)
    {
        const auto* tdef = dom->find_type_by_name(nd.type_name.c_str());
        if (!tdef)
        {
            log::warn("[audio] group: unknown node type '%s' — skipped", nd.type_name.c_str());
            continue;
        }
        const int type_id = tdef->type_id;

        std::shared_ptr<audio_graph::node> node_ptr;

        if (type_id == GRPI_TYPE)
        {
            node_ptr = std::make_shared<audio_graph::mixer_node>(d->next_ag_id++);
            result.input_ag_id = node_ptr->id();
        }
        else if (type_id == GRPO_TYPE)
        {
            node_ptr = std::make_shared<audio_graph::mixer_node>(d->next_ag_id++);
            result.output_ag_id = node_ptr->id();
        }
        else if (type_id == GROUP_TYPE)
        {
            entt::id_type res_id = pid(nd, "res_id");
            if (!res_id) { log::warn("[audio] group: nested GROUP has no res_id"); continue; }
            auto sub_res = rman().get<rgraphplan>(res_id);
            if (!sub_res)
            {
                log::warn("[audio] group: nested GROUP rgraphplan %x not found", res_id);
                continue;
            }
            auto r = _expand_group_impl(d, new_graph, *sub_res, depth + 1);
            if (r.output_ag_id >= 0) sub_id_map[nd.id]           = r.output_ag_id;
            if (r.input_ag_id  >= 0) sub_group_input_map[nd.id]  = r.input_ag_id;
            continue;
        }
        else if (type_id == static_cast<int>(NT::GAIN))
        {
            node_ptr = std::make_shared<audio_graph::gain_node>(d->next_ag_id++, pf(nd, "gain_db", 0.f));
        }
        else if (type_id == static_cast<int>(NT::REVERB))
        {
            node_ptr = std::make_shared<audio_graph::reverb_node>(d->next_ag_id++,
                pf(nd, "room_size", 0.5f), pf(nd, "damping", 0.5f), pf(nd, "wet", 0.33f));
        }
        else if (type_id == static_cast<int>(NT::COMPRESSOR))
        {
            node_ptr = std::make_shared<audio_graph::compressor_node>(d->next_ag_id++,
                pf(nd, "threshold_db", -18.f), pf(nd, "ratio", 4.f),
                pf(nd, "attack_ms", 10.f), pf(nd, "release_ms", 100.f), pf(nd, "makeup_db", 0.f));
        }
        else if (type_id == static_cast<int>(NT::EQ5))
        {
            auto en = std::make_shared<audio_graph::eq5_node>(d->next_ag_id++);
            char key[32];
            for (int b = 0; b < 5; ++b)
            {
                snprintf(key, sizeof(key), "band%d_freq",    b); float freq = pf(nd, key, 0.f);
                snprintf(key, sizeof(key), "band%d_gain_db", b); float gain = pf(nd, key, 0.f);
                snprintf(key, sizeof(key), "band%d_q",       b); float q    = pf(nd, key, 0.707f);
                en->set_band(static_cast<size_t>(b), freq, gain, q);
            }
            node_ptr = std::move(en);
        }
        else if (type_id == static_cast<int>(NT::BITCRUSHER))
        {
            node_ptr = std::make_shared<audio_graph::bitcrusher_node>(d->next_ag_id++,
                pf(nd, "bits", 8.f), pf(nd, "downsample", 1.f));
        }
        else if (type_id == static_cast<int>(NT::DELAY))
        {
            node_ptr = std::make_shared<audio_graph::delay_node>(d->next_ag_id++,
                pf(nd, "delay_ms", 250.f), pf(nd, "feedback", 0.4f), pf(nd, "mix", 0.5f));
        }
        else if (type_id == static_cast<int>(NT::RING_MOD))
        {
            node_ptr = std::make_shared<audio_graph::ring_mod_node>(d->next_ag_id++,
                pf(nd, "carrier_hz", 200.f), pf(nd, "mix", 1.f));
        }
        else if (type_id == static_cast<int>(NT::CHORUS))
        {
            node_ptr = std::make_shared<audio_graph::chorus_node>(d->next_ag_id++,
                pf(nd, "rate_hz", 0.5f), pf(nd, "depth_ms", 8.f),
                pf(nd, "voices", 2.f),   pf(nd, "mix", 0.5f));
        }
        else if (type_id == static_cast<int>(NT::WAVESHAPER))
        {
            node_ptr = std::make_shared<audio_graph::waveshaper_node>(d->next_ag_id++,
                pf(nd, "drive", 5.f), pf(nd, "shape", 0.f), pf(nd, "mix", 1.f));
        }
        else if (type_id == static_cast<int>(NT::PHASER))
        {
            node_ptr = std::make_shared<audio_graph::phaser_node>(d->next_ag_id++,
                pf(nd, "rate_hz", 0.5f), pf(nd, "depth", 0.8f),
                pf(nd, "stages", 4.f),   pf(nd, "feedback", 0.5f), pf(nd, "mix", 0.5f));
        }
        else if (type_id == static_cast<int>(NT::PITCH))
        {
            node_ptr = std::make_shared<audio_graph::pitch_node>(d->next_ag_id++,
                pf(nd, "pitch_ratio", 1.f));
        }
        else if (type_id == static_cast<int>(NT::MIXER))
        {
            node_ptr = std::make_shared<audio_graph::mixer_node>(d->next_ag_id++);
        }
        else if (type_id == static_cast<int>(NT::VORBIS_SOURCE))
        {
            entt::id_type res_id = pid(nd, "res_id");
            if (!res_id) continue;
            auto vres = rman().get<rvorbis>(res_id);
            if (!vres || !vres->valid) { log::warn("[audio] group vorbis: resource %x not found", res_id); continue; }
            auto vp = std::make_unique<audio_producer_vorbis>(vres);
            if (!vp->is_valid()) { log::warn("[audio] group vorbis: producer init failed"); continue; }
            bool loop = pb(nd, "loop", false);
            auto prod = std::make_unique<audio_producer_looper>(
                std::shared_ptr<audio_producer>(std::move(vp)), 0, loop ? 0 : 1);
            node_ptr = std::make_shared<audio_graph::source_node>(d->next_ag_id++, std::move(prod));
        }
        else if (type_id == static_cast<int>(NT::SINE_SOURCE))
        {
            node_ptr = std::make_shared<audio_graph::source_node>(d->next_ag_id++,
                std::make_unique<audio_producer_sine>(new_graph.spec(),
                    pf(nd, "frequency", 440.f), pf(nd, "amplitude", 0.5f)));
        }
        else if (type_id == static_cast<int>(NT::NOISE_SOURCE))
        {
            node_ptr = std::make_shared<audio_graph::source_node>(d->next_ag_id++,
                std::make_unique<audio_producer_noise>(new_graph.spec(),
                    pf(nd, "amplitude", 0.5f)));
        }
        else
        {
            log::warn("[audio] group: node type %d ('%s') not supported in subgraphs — skipped",
                      type_id, nd.type_name.c_str());
            continue;
        }

        if (node_ptr)
        {
            new_graph.add_node(node_ptr);
            sub_id_map[nd.id] = node_ptr->id();
        }
    }

    for (const auto& ld : subgp.links)
    {
        auto from_it = sub_id_map.find(ld.from_node);
        if (from_it == sub_id_map.end()) continue;

        audio_graph::node_id ag_dst;
        auto gin_it = sub_group_input_map.find(ld.to_node);
        if (gin_it != sub_group_input_map.end())
            ag_dst = gin_it->second;
        else
        {
            auto to_it = sub_id_map.find(ld.to_node);
            if (to_it == sub_id_map.end()) continue;
            ag_dst = to_it->second;
        }
        new_graph.connect(from_it->second, ag_dst);
    }

    if (result.input_ag_id < 0)
        log::warn("[audio] group subgraph has no Group Input node");
    if (result.output_ag_id < 0)
        log::warn("[audio] group subgraph has no Group Output node");

    log::verb("[audio] expanded group depth=%d: input_ag=%d output_ag=%d nodes=%zu",
              depth, result.input_ag_id, result.output_ag_id, subgp.nodes.size());
    return result;
}

void audio_graph_manager::rebuild(audio_graph::graph& live_graph, SDL_Mutex* mtx)
{
    assert(_d->gplan);

    const int OUTPUT_TYPE    = static_cast<int>(audio_graph::node_type::OUTPUT);
    const int SOURCE_TYPE    = static_cast<int>(audio_graph::node_type::SOURCE);
    const int MIXER_TYPE     = static_cast<int>(audio_graph::node_type::MIXER);
    const int VORBIS_TYPE    = static_cast<int>(audio_graph::node_type::VORBIS_SOURCE);
    const int SINE_TYPE      = static_cast<int>(audio_graph::node_type::SINE_SOURCE);
    const int NOISE_TYPE     = static_cast<int>(audio_graph::node_type::NOISE_SOURCE);
    const int REVERB_TYPE    = static_cast<int>(audio_graph::node_type::REVERB);
    const int GAIN_TYPE      = static_cast<int>(audio_graph::node_type::GAIN);
    const int BUS_INPUT_TYPE = static_cast<int>(audio_graph::node_type::BUS_INPUT);
    const int BUS_OUTPUT_TYPE= static_cast<int>(audio_graph::node_type::BUS_OUTPUT);
    const int COMPRESSOR_TYPE= static_cast<int>(audio_graph::node_type::COMPRESSOR);
    const int EQ5_TYPE       = static_cast<int>(audio_graph::node_type::EQ5);
    const int VISUALIZER_TYPE= static_cast<int>(audio_graph::node_type::VISUALIZER);
    const int BITCRUSHER_TYPE= static_cast<int>(audio_graph::node_type::BITCRUSHER);
    const int DELAY_TYPE     = static_cast<int>(audio_graph::node_type::DELAY);
    const int RING_MOD_TYPE    = static_cast<int>(audio_graph::node_type::RING_MOD);
    const int CHORUS_TYPE      = static_cast<int>(audio_graph::node_type::CHORUS);
    const int WAVESHAPER_TYPE  = static_cast<int>(audio_graph::node_type::WAVESHAPER);
    const int PHASER_TYPE      = static_cast<int>(audio_graph::node_type::PHASER);
    const int PITCH_TYPE       = static_cast<int>(audio_graph::node_type::PITCH);
#ifdef NEWBASE_TALKIE_PCM
    const int TALKIE_PCM_TYPE  = static_cast<int>(audio_graph::node_type::TALKIE_PCM_SOURCE);
#endif
    const int LPC_TYPE         = static_cast<int>(audio_graph::node_type::LPC_SOURCE);
    const int GROUP_TYPE       = static_cast<int>(audio_graph::node_type::GROUP);

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
    auto prop_id = [](const graphplan::node_data& nd, const char* key) -> entt::id_type {
        auto it = nd.properties.find(key);
        if (it == nd.properties.end()) return 0;
        if (const entt::id_type* v = it->second.try_cast<entt::id_type>()) return *v;
        return 0;
    };

    auto load_lpc_vocab = [&](const graphplan::node_data& nd, uint64_t gp_id)
        -> std::shared_ptr<rlpcvocab>
    {
        entt::id_type res_id = prop_id(nd, "vocab_res_id");
        if (res_id == 0) { _d->lpc_vocab_cache[gp_id] = nullptr; return nullptr; }
        auto vres = rman().get<rlpcvocab>(res_id);
        _d->lpc_vocab_cache[gp_id] = vres;
        if (!vres || !vres->valid)
            log::warn("[audio] lpc_node %llu: vocab resource not found", gp_id);
        return vres;
    };

    auto make_vorbis_producer = [&](const graphplan::node_data& nd, uint64_t gp_id)
        -> std::unique_ptr<audio_producer>
    {
        entt::id_type res_id = prop_id(nd, "res_id");
        if (res_id == 0) { _d->vorbis_res_cache[gp_id] = nullptr; return nullptr; }

        auto vres = rman().get<rvorbis>(res_id);
        _d->vorbis_res_cache[gp_id] = vres;
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
        const size_t loop_count = prop_b(nd, "loop", false) ? 0 : 1;
        return std::make_unique<audio_producer_looper>(
            std::shared_ptr<audio_producer>(std::move(vp)), 0, loop_count);
    };

    std::unordered_map<uint64_t, audio_graph::node_id> id_map;
    std::unordered_map<uint64_t, audio_graph::node_id> group_input_map;

    audio_graph::graph new_graph;
    new_graph.set_spec(live_graph.spec());

    // Bus pre-pass.
    std::unordered_map<std::string, std::shared_ptr<audio_graph::mixer_node>> active_buses;
    for (const auto& [gp_id, nd] : _d->gplan->nodes)
    {
        if (nd.type != BUS_INPUT_TYPE && nd.type != BUS_OUTPUT_TYPE) continue;
        const std::string* id_ptr = prop_s(nd, "bus_id");
        const std::string  bus_id = id_ptr ? *id_ptr : std::string{};
        if (bus_id.empty() || active_buses.count(bus_id)) continue;

        std::shared_ptr<audio_graph::mixer_node> bus_mixer;
        auto cache_it = _d->bus_cache.find(bus_id);
        if (cache_it != _d->bus_cache.end())
        {
            bus_mixer = cache_it->second;
            log::verb("[audio] reused bus mixer '%s' ag=%d", bus_id.c_str(), bus_mixer->id());
        }
        else
        {
            audio_graph::node_id ag_id = _d->next_ag_id++;
            bus_mixer = std::make_shared<audio_graph::mixer_node>(ag_id);
            _d->bus_cache[bus_id] = bus_mixer;
            log::verb("[audio] created bus mixer '%s' ag=%d", bus_id.c_str(), ag_id);
        }
        new_graph.add_node(bus_mixer);
        active_buses[bus_id] = bus_mixer;
    }
    for (auto it = _d->bus_cache.begin(); it != _d->bus_cache.end(); )
        it = active_buses.count(it->first) ? std::next(it) : _d->bus_cache.erase(it);

    for (const auto& [gp_id, nd] : _d->gplan->nodes)
    {
        if (nd.type == OUTPUT_TYPE)
        {
            id_map[gp_id] = 0;
            _d->node_cache[gp_id] = new_graph.nodes().at(0);
            continue;
        }

        if (nd.type == BUS_INPUT_TYPE || nd.type == BUS_OUTPUT_TYPE)
        {
            const std::string* id_ptr = prop_s(nd, "bus_id");
            const std::string  bus_id = id_ptr ? *id_ptr : std::string{};
            auto it = active_buses.find(bus_id);
            if (it != active_buses.end())
                id_map[gp_id] = it->second->id();
            continue;
        }

        if (nd.type == GROUP_TYPE)
        {
            entt::id_type res_id = prop_id(nd, "res_id");
            if (!res_id) { log::warn("[audio] GROUP node %llu has no res_id", gp_id); continue; }
            auto sub_res = rman().get<rgraphplan>(res_id);
            if (!sub_res)
            {
                log::warn("[audio] GROUP node %llu: rgraphplan resource %x not found", gp_id, res_id);
                continue;
            }
            auto r = _expand_group_impl(_d, new_graph, *sub_res, 1);
            if (r.output_ag_id >= 0)
            {
                id_map[gp_id] = r.output_ag_id;
                _d->node_cache[gp_id] = new_graph.nodes().at(r.output_ag_id);
            }
            if (r.input_ag_id >= 0)
                group_input_map[gp_id] = r.input_ag_id;
            continue;
        }

        auto cache_it = _d->node_cache.find(gp_id);
        const bool have_cached = cache_it != _d->node_cache.end()
                                 && cache_it->second != nullptr;
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
            else if (nd.type == VISUALIZER_TYPE)
                type_matches = dynamic_cast<audio_graph::visualizer_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == BITCRUSHER_TYPE)
                type_matches = dynamic_cast<audio_graph::bitcrusher_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == DELAY_TYPE)
                type_matches = dynamic_cast<audio_graph::delay_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == RING_MOD_TYPE)
                type_matches = dynamic_cast<audio_graph::ring_mod_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == CHORUS_TYPE)
                type_matches = dynamic_cast<audio_graph::chorus_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == WAVESHAPER_TYPE)
                type_matches = dynamic_cast<audio_graph::waveshaper_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == PHASER_TYPE)
                type_matches = dynamic_cast<audio_graph::phaser_node*>(cache_it->second.get()) != nullptr;
            else if (nd.type == PITCH_TYPE)
                type_matches = dynamic_cast<audio_graph::pitch_node*>(cache_it->second.get()) != nullptr;
#ifdef NEWBASE_TALKIE_PCM
            else if (nd.type == TALKIE_PCM_TYPE)
                type_matches = dynamic_cast<audio_graph::talkie_pcm_node*>(cache_it->second.get()) != nullptr;
#endif
            else if (nd.type == LPC_TYPE)
                type_matches = dynamic_cast<audio_graph::lpc_node*>(cache_it->second.get()) != nullptr;
            else
                type_matches = dynamic_cast<audio_graph::source_node*>(cache_it->second.get()) != nullptr;
        }

        std::shared_ptr<audio_graph::node> node_ptr;

        if (have_cached && type_matches)
        {
            node_ptr = cache_it->second;

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
                entt::id_type new_res_id = prop_id(nd, "res_id");
                auto& cached_res = _d->vorbis_res_cache[gp_id];
                bool res_changed = !cached_res || cached_res->id() != new_res_id;

                auto& fb_ptr = _d->vorbis_fb_cache[gp_id];
                if (!fb_ptr) fb_ptr = std::make_shared<nb::vorbis_feedback>();
                _d->gplan->nodes.at(gp_id).user_data = fb_ptr;

                if (res_changed)
                {
                    auto new_prod = make_vorbis_producer(nd, gp_id);
                    if (new_prod)
                        static_cast<audio_producer_looper*>(new_prod.get())->set_feedback(fb_ptr);
                    sn->set_producer(std::move(new_prod));
                }
                else
                {
                    const size_t loop_count = prop_b(nd, "loop", false) ? 0 : 1;
                    if (auto* lp = dynamic_cast<audio_producer_looper*>(sn->producer()))
                    {
                        lp->set_loop_count(loop_count);
                        lp->set_feedback(fb_ptr);
                    }
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
                static_cast<audio_graph::gain_node*>(node_ptr.get())->set_gain_db(prop_f(nd, "gain_db", 0.f));
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
            else if (nd.type == VISUALIZER_TYPE)
            {
                auto& fb_ptr = _d->vis_fb_cache[gp_id];
                if (!fb_ptr) fb_ptr = std::make_shared<visualizer_feedback>();
                _d->gplan->nodes.at(gp_id).user_data = fb_ptr;
                static_cast<audio_graph::visualizer_node*>(node_ptr.get())->set_feedback(fb_ptr);
                new_graph.mark_always_pull(node_ptr->id());
            }
            else if (nd.type == BITCRUSHER_TYPE)
            {
                static_cast<audio_graph::bitcrusher_node*>(node_ptr.get())
                    ->set_params(prop_f(nd, "bits", 8.f), prop_f(nd, "downsample", 1.f));
            }
            else if (nd.type == DELAY_TYPE)
            {
                static_cast<audio_graph::delay_node*>(node_ptr.get())
                    ->set_params(prop_f(nd, "delay_ms", 250.f),
                                 prop_f(nd, "feedback", 0.4f),
                                 prop_f(nd, "mix",      0.5f));
            }
            else if (nd.type == RING_MOD_TYPE)
            {
                static_cast<audio_graph::ring_mod_node*>(node_ptr.get())
                    ->set_params(prop_f(nd, "carrier_hz", 200.f), prop_f(nd, "mix", 1.f));
            }
            else if (nd.type == CHORUS_TYPE)
            {
                static_cast<audio_graph::chorus_node*>(node_ptr.get())
                    ->set_params(prop_f(nd, "rate_hz", 0.5f), prop_f(nd, "depth_ms", 8.f),
                                 prop_f(nd, "voices", 2.f),   prop_f(nd, "mix", 0.5f));
            }
            else if (nd.type == WAVESHAPER_TYPE)
            {
                static_cast<audio_graph::waveshaper_node*>(node_ptr.get())
                    ->set_params(prop_f(nd, "drive", 5.f), prop_f(nd, "shape", 0.f),
                                 prop_f(nd, "mix", 1.f));
            }
            else if (nd.type == PHASER_TYPE)
            {
                static_cast<audio_graph::phaser_node*>(node_ptr.get())
                    ->set_params(prop_f(nd, "rate_hz", 0.5f), prop_f(nd, "depth", 0.8f),
                                 prop_f(nd, "stages", 4.f),   prop_f(nd, "feedback", 0.5f),
                                 prop_f(nd, "mix", 0.5f));
            }
            else if (nd.type == PITCH_TYPE)
            {
                static_cast<audio_graph::pitch_node*>(node_ptr.get())
                    ->set_pitch_ratio(prop_f(nd, "pitch_ratio", 1.f));
            }
#ifdef NEWBASE_TALKIE_PCM
            else if (nd.type == TALKIE_PCM_TYPE)
            {
                auto* tn = static_cast<audio_graph::talkie_pcm_node*>(node_ptr.get());
                tn->set_volume(prop_f(nd, "volume", 1.f));
                auto& fb_ptr = _d->talkie_pcm_fb_cache[gp_id];
                if (!fb_ptr) fb_ptr = std::make_shared<audio_graph::talkie_pcm_feedback>();
                fb_ptr->node_wptr = std::static_pointer_cast<audio_graph::talkie_pcm_node>(node_ptr);
                _d->gplan->nodes.at(gp_id).user_data = fb_ptr;
            }
#endif
            else if (nd.type == LPC_TYPE)
            {
                auto* ln = static_cast<audio_graph::lpc_node*>(node_ptr.get());
                ln->set_volume(prop_f(nd, "volume", 1.f));
                auto vocab = load_lpc_vocab(nd, gp_id);
                ln->set_vocab(vocab);
                auto& fb_ptr = _d->lpc_fb_cache[gp_id];
                if (!fb_ptr) fb_ptr = std::make_shared<audio_graph::lpc_feedback>();
                fb_ptr->node_wptr = std::static_pointer_cast<audio_graph::lpc_node>(node_ptr);
                fb_ptr->vocab     = vocab;
                _d->gplan->nodes.at(gp_id).user_data = fb_ptr;
            }

            new_graph.add_node(node_ptr);
            log::verb("[audio] reused node gp=%llu ag=%d", gp_id, node_ptr->id());
        }
        else
        {
            audio_graph::node_id ag_id = _d->next_ag_id++;

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
                auto raw_prod = make_vorbis_producer(nd, gp_id);
                auto& fb_ptr = _d->vorbis_fb_cache[gp_id];
                if (!fb_ptr) fb_ptr = std::make_shared<nb::vorbis_feedback>();
                _d->gplan->nodes.at(gp_id).user_data = fb_ptr;
                if (raw_prod)
                    static_cast<audio_producer_looper*>(raw_prod.get())->set_feedback(fb_ptr);
                node_ptr = std::make_shared<audio_graph::source_node>(ag_id, std::move(raw_prod));
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
                node_ptr = std::make_shared<audio_graph::gain_node>(ag_id, prop_f(nd, "gain_db", 0.f));
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
            else if (nd.type == VISUALIZER_TYPE)
            {
                auto& fb_ptr = _d->vis_fb_cache[gp_id];
                if (!fb_ptr) fb_ptr = std::make_shared<visualizer_feedback>();
                _d->gplan->nodes.at(gp_id).user_data = fb_ptr;
                auto vn = std::make_shared<audio_graph::visualizer_node>(ag_id);
                vn->set_feedback(fb_ptr);
                node_ptr = std::move(vn);
                // Visualizer is a side-effect sink — must be pulled even with no outgoing edges.
                new_graph.mark_always_pull(ag_id);
            }
            else if (nd.type == BITCRUSHER_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::bitcrusher_node>(ag_id,
                    prop_f(nd, "bits", 8.f), prop_f(nd, "downsample", 1.f));
            }
            else if (nd.type == DELAY_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::delay_node>(ag_id,
                    prop_f(nd, "delay_ms", 250.f),
                    prop_f(nd, "feedback", 0.4f),
                    prop_f(nd, "mix",      0.5f));
            }
            else if (nd.type == RING_MOD_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::ring_mod_node>(ag_id,
                    prop_f(nd, "carrier_hz", 200.f), prop_f(nd, "mix", 1.f));
            }
            else if (nd.type == CHORUS_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::chorus_node>(ag_id,
                    prop_f(nd, "rate_hz", 0.5f), prop_f(nd, "depth_ms", 8.f),
                    prop_f(nd, "voices", 2.f),   prop_f(nd, "mix", 0.5f));
            }
            else if (nd.type == WAVESHAPER_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::waveshaper_node>(ag_id,
                    prop_f(nd, "drive", 5.f), prop_f(nd, "shape", 0.f), prop_f(nd, "mix", 1.f));
            }
            else if (nd.type == PHASER_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::phaser_node>(ag_id,
                    prop_f(nd, "rate_hz", 0.5f), prop_f(nd, "depth", 0.8f),
                    prop_f(nd, "stages", 4.f),   prop_f(nd, "feedback", 0.5f),
                    prop_f(nd, "mix", 0.5f));
            }
            else if (nd.type == PITCH_TYPE)
            {
                node_ptr = std::make_shared<audio_graph::pitch_node>(ag_id,
                    prop_f(nd, "pitch_ratio", 1.f));
            }
#ifdef NEWBASE_TALKIE_PCM
            else if (nd.type == TALKIE_PCM_TYPE)
            {
                auto tn = std::make_shared<audio_graph::talkie_pcm_node>(ag_id);
                tn->set_volume(prop_f(nd, "volume", 1.f));
                auto& fb_ptr = _d->talkie_pcm_fb_cache[gp_id];
                if (!fb_ptr) fb_ptr = std::make_shared<audio_graph::talkie_pcm_feedback>();
                fb_ptr->node_wptr = tn;
                _d->gplan->nodes.at(gp_id).user_data = fb_ptr;
                node_ptr = std::move(tn);
            }
#endif
            else if (nd.type == LPC_TYPE)
            {
                auto ln = std::make_shared<audio_graph::lpc_node>(ag_id);
                ln->set_volume(prop_f(nd, "volume", 1.f));
                auto vocab = load_lpc_vocab(nd, gp_id);
                ln->set_vocab(vocab);
                auto& fb_ptr = _d->lpc_fb_cache[gp_id];
                if (!fb_ptr) fb_ptr = std::make_shared<audio_graph::lpc_feedback>();
                fb_ptr->node_wptr = ln;
                fb_ptr->vocab     = vocab;
                _d->gplan->nodes.at(gp_id).user_data = fb_ptr;
                node_ptr = std::move(ln);
            }
            else
            {
                log::warn("[audio] graphplan node %llu has unknown type %d — skipped", gp_id, nd.type);
                continue;
            }

            new_graph.add_node(node_ptr);
            _d->node_cache[gp_id] = node_ptr;
            log::verb("[audio] created node gp=%llu ag=%d type=%d", gp_id, ag_id, nd.type);
        }

        id_map[gp_id] = node_ptr->id();
    }

    // Evict stale cache entries.
    for (auto it = _d->node_cache.begin(); it != _d->node_cache.end(); )
    {
        if (!_d->gplan->nodes.count(it->first))
        {
            _d->vorbis_res_cache.erase(it->first);
            _d->vorbis_fb_cache.erase(it->first);
#ifdef NEWBASE_TALKIE_PCM
            _d->talkie_pcm_fb_cache.erase(it->first);
#endif
            _d->lpc_fb_cache.erase(it->first);
            _d->lpc_vocab_cache.erase(it->first);
            auto vit = _d->vis_fb_cache.find(it->first);
            if (vit != _d->vis_fb_cache.end())
            {
                auto* rs = entt::locator<renderer_service*>::has_value()
                           ? entt::locator<renderer_service*>::value() : nullptr;
                if (rs && vit->second && vit->second->texture)
                    rs->destroy_texture(vit->second->texture);
                _d->vis_fb_cache.erase(vit);
            }
            it = _d->node_cache.erase(it);
        }
        else
            ++it;
    }

    // Fan-out pass: detect graphplan nodes whose output feeds > 1 consumer.
    // For each such node, inject an internal fan_out_node and route all consumers
    // through it so the upstream node is only pulled once per generation.
    {
        // Count outgoing links per graphplan source node.
        std::unordered_map<uint64_t, int> out_degree;
        for (const auto& [link_id, lk] : _d->gplan->links)
        {
            auto src_pin_it = _d->gplan->pins.find(lk.output_pin);
            if (src_pin_it != _d->gplan->pins.end())
                ++out_degree[src_pin_it->second.node_id];
        }

        // Create/reuse fan_out nodes and connect source → fan_out.
        std::unordered_map<uint64_t, audio_graph::node_id> fan_out_id_map;
        for (const auto& [gp_id, deg] : out_degree)
        {
            if (deg <= 1) continue;
            auto src_it = id_map.find(gp_id);
            if (src_it == id_map.end()) continue;

            auto cache_it = _d->fan_out_cache.find(gp_id);
            std::shared_ptr<audio_graph::fan_out_node> fo;
            if (cache_it != _d->fan_out_cache.end())
            {
                fo = cache_it->second;
                log::verb("[audio] reused fan_out node gp=%llu ag=%d", gp_id, fo->id());
            }
            else
            {
                audio_graph::node_id ag_id = _d->next_ag_id++;
                fo = std::make_shared<audio_graph::fan_out_node>(ag_id);
                _d->fan_out_cache[gp_id] = fo;
                log::verb("[audio] created fan_out node gp=%llu ag=%d (out_degree=%d)",
                          gp_id, ag_id, deg);
            }
            new_graph.add_node(fo);
            fan_out_id_map[gp_id] = fo->id();
            new_graph.connect(src_it->second, fo->id());
        }

        // Evict fan_out nodes whose graphplan source no longer fans out.
        for (auto it = _d->fan_out_cache.begin(); it != _d->fan_out_cache.end(); )
        {
            auto deg_it = out_degree.find(it->first);
            it = (deg_it == out_degree.end() || deg_it->second <= 1)
                 ? _d->fan_out_cache.erase(it) : std::next(it);
        }

        // Wire edges, routing through fan_out nodes where applicable.
        for (const auto& [link_id, lk] : _d->gplan->links)
        {
            auto src_pin_it = _d->gplan->pins.find(lk.output_pin);
            auto dst_pin_it = _d->gplan->pins.find(lk.input_pin);
            if (src_pin_it == _d->gplan->pins.end() || dst_pin_it == _d->gplan->pins.end())
            {
                log::warn("[audio] graphplan link %llu references unknown pin — skipped", link_id);
                continue;
            }
            const uint64_t src_gp_id = src_pin_it->second.node_id;
            const uint64_t dst_gp_id = dst_pin_it->second.node_id;

            // Use the fan_out node as source if one was injected.
            audio_graph::node_id ag_src;
            auto fo_it = fan_out_id_map.find(src_gp_id);
            if (fo_it != fan_out_id_map.end())
            {
                ag_src = fo_it->second;
            }
            else
            {
                auto src_it = id_map.find(src_gp_id);
                if (src_it == id_map.end())
                {
                    log::warn("[audio] graphplan link %llu src node unmapped — skipped", link_id);
                    continue;
                }
                ag_src = src_it->second;
            }

            audio_graph::node_id ag_dst;
            {
                auto gin_it = group_input_map.find(dst_gp_id);
                if (gin_it != group_input_map.end())
                {
                    ag_dst = gin_it->second;
                }
                else
                {
                    auto dst_it = id_map.find(dst_gp_id);
                    if (dst_it == id_map.end())
                    {
                        log::warn("[audio] graphplan link %llu dst node unmapped — skipped", link_id);
                        continue;
                    }
                    ag_dst = dst_it->second;
                }
            }
            new_graph.connect(ag_src, ag_dst);
        }
    }

    // Debug log.
    {
        const auto& nodes = new_graph.nodes();
        const auto& edges = new_graph.edges();
        log::verb("[audio] --- graph rebuilt: %zu nodes, %zu edges ---", nodes.size(), edges.size());
        auto order = new_graph.topological_sort(nullptr);
        for (auto ag_id : order)
        {
            auto* n = nodes.at(ag_id).get();
            size_t out_edges = edges.count(ag_id) ? edges.at(ag_id).size() : 0;
            uint64_t gp_id_display = 0;
            for (const auto& [gp, ag] : id_map)
                if (ag == ag_id) { gp_id_display = gp; break; }
            const auto* gp_nd = _d->gplan->nodes.count(gp_id_display)
                                 ? &_d->gplan->nodes.at(gp_id_display) : nullptr;
            const char* type_name = "?";
            if (gp_nd)
            {
                const int t = gp_nd->type;
                if      (t == OUTPUT_TYPE) type_name = "Output";
                else if (t == SOURCE_TYPE) type_name = "Source";
                else if (t == MIXER_TYPE)  type_name = "Mixer";
                else if (t == VORBIS_TYPE) type_name = "Vorbis";
                else if (t == SINE_TYPE)   type_name = "Sine";
                else if (t == NOISE_TYPE)  type_name = "Noise";
                else if (t == REVERB_TYPE) type_name = "Reverb";
            }
            log::verb("[audio]   node ag=%d (gp=%llu) type=%s out_edges=%zu",
                      ag_id, gp_id_display, type_name, out_edges);
            if (gp_nd && gp_nd->type == VORBIS_TYPE)
            {
                entt::id_type res_id = prop_id(*gp_nd, "res_id");
                const bool* loop_ptr = [&]() -> const bool* {
                    auto it = gp_nd->properties.find("loop");
                    return it != gp_nd->properties.end() ? it->second.try_cast<bool>() : nullptr;
                }();
                const char* loop_str = loop_ptr ? (*loop_ptr ? "1" : "0") : "(none)";
                auto* sn = dynamic_cast<audio_graph::source_node*>(n);
                log::verb("[audio]     res_id=%x loop=%s producer=%s",
                          res_id, loop_str, (sn && sn->producer()) ? "ok" : "NULL");
            }
            for (const auto& [src, dsts] : edges)
                for (auto dst : dsts)
                    if (dst == ag_id)
                        log::verb("[audio]     <- from ag=%d", src);
        }
        log::verb("[audio] --- end graph ---");
    }

    SDL_LockMutex(mtx);
    live_graph = std::move(new_graph);
    SDL_UnlockMutex(mtx);
}
