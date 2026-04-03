#include <newbase/editor/audio_editor_widget.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace nb {

static constexpr int kWaveformPoints = 4096;

void audio_editor_widget::open(const void* pcm_data, size_t byte_len, audio_spec spec)
{
    _waveform.clear();
    _spec         = spec;
    _total_frames = 0;

    int sample_size = (int)audio_format_size(spec.format);
    if (sample_size == 0 || spec.channels <= 0 || byte_len == 0) return;

    size_t frame_size = (size_t)sample_size * spec.channels;
    _total_frames = byte_len / frame_size;
    if (_total_frames == 0) return;

    int n = (int)std::min((size_t)kWaveformPoints, _total_frames);
    _waveform.resize(n);

    const char* base = static_cast<const char*>(pcm_data);

    for (int i = 0; i < n; i++)
    {
        size_t f0 = (size_t)i       * _total_frames / n;
        size_t f1 = (size_t)(i + 1) * _total_frames / n;
        if (f1 <= f0) f1 = f0 + 1;
        f1 = std::min(f1, _total_frames);

        float peak = 0.0f;

        for (size_t f = f0; f < f1; f++)
        {
            float mixed = 0.0f;
            for (int c = 0; c < spec.channels; c++)
            {
                size_t off = (f * spec.channels + c) * sample_size;
                float s = 0.0f;
                switch (spec.format)
                {
                case audio_format::S16: {
                    int16_t v; memcpy(&v, base + off, 2);
                    s = v / 32768.0f;
                    break;
                }
                case audio_format::FLOAT: {
                    float v; memcpy(&v, base + off, 4);
                    s = v;
                    break;
                }
                case audio_format::S8: {
                    int8_t v; memcpy(&v, base + off, 1);
                    s = v / 128.0f;
                    break;
                }
                case audio_format::U8: {
                    uint8_t v; memcpy(&v, base + off, 1);
                    s = (v - 128) / 128.0f;
                    break;
                }
                default: break;
                }
                mixed += s;
            }
            mixed /= spec.channels;
            if (std::abs(mixed) > std::abs(peak)) peak = mixed;
        }

        _waveform[i] = peak;
    }
}

void audio_editor_widget::draw()
{
    if (_waveform.empty()) { ImGui::TextDisabled("(no audio data)"); return; }

    const char* fmt_str = "?";
    switch (_spec.format)
    {
    case audio_format::FLOAT: fmt_str = "f32"; break;
    case audio_format::S16:   fmt_str = "s16"; break;
    case audio_format::S8:    fmt_str = "s8";  break;
    case audio_format::U8:    fmt_str = "u8";  break;
    default: break;
    }

    float duration_s = (_spec.frequency > 0)
        ? (float)_total_frames / (float)_spec.frequency
        : 0.0f;
    ImGui::TextDisabled("%s  %dch  %dHz  %.2fs",
        fmt_str, _spec.channels, _spec.frequency, duration_s);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PlotLines("##waveform", _waveform.data(), (int)_waveform.size(),
                     0, nullptr, -1.0f, 1.0f, avail);
}

} // namespace nb
