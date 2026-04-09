#pragma once

#include <newbase/audio/graph/node.hpp>
#include <newbase/audio/producer.hpp>
#include <newbase/audio/converter.hpp>
#include <newbase/log.hpp>
#include <memory>
#include <cassert>
#include <cstring>
#include <vector>

namespace nb::audio_graph
{

// Add src samples into dst (float format only; asserts otherwise).
inline void mix_add(audio_buffer& dst, const audio_buffer& src, size_t frames)
{
    if (dst.spec().format != audio_format::FLOAT) assert(false);
    if (src.spec().format != audio_format::FLOAT) assert(false);
    assert(dst.spec().channels == src.spec().channels);
    float*       d     = reinterpret_cast<float*>(dst.data().data());
    const float* s     = reinterpret_cast<const float*>(src.data().data());
    size_t       count = frames * static_cast<size_t>(dst.spec().channels);
    for (size_t i = 0; i < count; ++i)
        d[i] += s[i];
}


// Final audio sink — mixes all inputs into one buffer (always node id=0).
class output_node : public node
{
public:
    explicit output_node(node_id id) : node(id) {}

    void process(audio_spec spec, size_t frames,
                 const std::vector<audio_buffer*>& inputs) override
    {
        log::info("[output_node %d] processing NUM_INPUTS=%zu frames=%zu", id(), inputs.size(), frames);
        ensure_buf(spec, frames);
        std::fill(buf_->data().begin(), buf_->data().end(), std::byte{0});
        for (auto* input : inputs)
        {
            if (input && input->frames() >= frames)
                mix_add(*buf_, *input, frames);
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    std::unique_ptr<audio_buffer> buf_;

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }
};


// Wraps an audio_producer as a graph source (no inputs).
// Producer may be null; outputs silence in that case.
// If the producer's spec differs from the graph spec, converts automatically.
class source_node : public node
{
public:
    explicit source_node(node_id id, std::unique_ptr<audio_producer> producer = nullptr)
        : node(id), producer_(std::move(producer))
    {
    }

    void process(audio_spec graph_spec, size_t frames,
                 const std::vector<audio_buffer*>& /*inputs*/) override
    {
        ensure_out_buf(graph_spec, frames);
        std::fill(out_buf_->data().begin(), out_buf_->data().end(), std::byte{0});

        if (!producer_)
            return;

        const audio_spec prod_spec = producer_->spec();

        if (prod_spec == graph_spec)
        {
            log::info("[source_node %d] direct pull, frames=%zu", id(), frames);
            auto sp = out_buf_->as_span();
            producer_->frames_pull(std::move(sp), frames);
        }
        else
        {
            // How many producer frames are needed to supply `frames` output frames?
            // Subtract frames already buffered in the converter, then ceiling-round.
            ensure_converter(prod_spec, graph_spec);
            const size_t already = converter_->available();
            const size_t need    = already >= frames ? 0 : frames - already;
            const size_t input_frames = (prod_spec.frequency == graph_spec.frequency)
                ? need
                : (need * static_cast<size_t>(prod_spec.frequency) + graph_spec.frequency - 1)
                  / graph_spec.frequency;

            if (input_frames > 0)
            {
                ensure_scratch_buf(prod_spec, input_frames);
                std::fill(scratch_buf_->data().begin(), scratch_buf_->data().end(), std::byte{0});
                auto sp = scratch_buf_->as_span();
                producer_->frames_pull(std::move(sp), input_frames);

                auto scratch_sp = scratch_buf_->as_span();
                converter_->put(scratch_sp);
            }
            auto out_sp = out_buf_->as_span();
            converter_->take(std::move(out_sp));
        }
    }

    audio_buffer* output_buffer() override { return out_buf_.get(); }

    audio_producer* producer() { return producer_.get(); }
    void set_producer(std::unique_ptr<audio_producer> p)
    {
        producer_ = std::move(p);
        // Reset converter so it is rebuilt for the new producer's spec.
        converter_.reset();
    }

private:
    std::unique_ptr<audio_producer>  producer_;
    std::unique_ptr<audio_buffer>    out_buf_;
    std::unique_ptr<audio_buffer>    scratch_buf_;
    std::unique_ptr<audio_converter> converter_;
    audio_spec                       converter_in_spec_;
    audio_spec                       converter_out_spec_;

    void ensure_out_buf(audio_spec spec, size_t frames)
    {
        if (!out_buf_ || out_buf_->spec() != spec || out_buf_->frames() != frames)
            out_buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }

    void ensure_scratch_buf(audio_spec spec, size_t frames)
    {
        if (!scratch_buf_ || scratch_buf_->spec() != spec || scratch_buf_->frames() != frames)
            scratch_buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }

    void ensure_converter(audio_spec in_spec, audio_spec out_spec)
    {
        if (!converter_ || converter_in_spec_ != in_spec || converter_out_spec_ != out_spec)
        {
            converter_      = std::make_unique<audio_converter>(in_spec, out_spec);
            converter_in_spec_  = in_spec;
            converter_out_spec_ = out_spec;
        }
    }
};


// Intermediate N-input mixer node.
class mixer_node : public node
{
public:
    explicit mixer_node(node_id id) : node(id) {}

    void process(audio_spec spec, size_t frames,
                 const std::vector<audio_buffer*>& inputs) override
    {
        ensure_buf(spec, frames);
        std::fill(buf_->data().begin(), buf_->data().end(), std::byte{0});
        for (auto* input : inputs)
        {
            if (input && input->frames() >= frames)
                mix_add(*buf_, *input, frames);
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    std::unique_ptr<audio_buffer> buf_;

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }
};

// Schroeder reverb: 4 parallel comb filters → 2 allpass filters in series.
// Operates on FLOAT interleaved audio. One set of delay lines per channel
// with slight per-channel detune for stereo spread.
// Props (all [0..1] unless noted): room_size, damping, wet.
class reverb_node : public node
{
public:
    reverb_node(node_id id, float room_size, float damping, float wet)
        : node(id), room_size_(room_size), damping_(damping), wet_(wet)
    {}

    void process(audio_spec spec, size_t frames,
                 const std::vector<audio_buffer*>& inputs) override
    {
        assert(spec.format == audio_format::FLOAT);
        ensure_buf(spec, frames);
        ensure_delay_lines(spec);

        const int   ch      = spec.channels;
        float*      out     = reinterpret_cast<float*>(buf_->data().data());
        const float dry     = 1.f - wet_;

        // Gather input (sum all upstream buffers).
        std::fill(buf_->data().begin(), buf_->data().end(), std::byte{0});
        for (auto* input : inputs)
        {
            if (!input || input->frames() < frames) continue;
            const float* src = reinterpret_cast<const float*>(input->data().data());
            for (size_t s = 0; s < frames * static_cast<size_t>(ch); ++s)
                out[s] += src[s];
        }

        // Process each channel independently through comb → allpass chain.
        for (int c = 0; c < ch; ++c)
        {
            auto& cl = ch_lines_[static_cast<size_t>(c)];
            for (size_t f = 0; f < frames; ++f)
            {
                float x = out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                float rev = 0.f;

                // 4 parallel comb filters.
                for (size_t i = 0; i < NUM_COMBS; ++i)
                {
                    auto& d   = cl.comb[i];
                    float buf_sample = d.buf[d.pos];
                    d.filter_store = buf_sample * (1.f - damping_) + d.filter_store * damping_;
                    d.buf[d.pos]   = x + d.filter_store * room_size_;
                    d.pos = (d.pos + 1) % d.buf.size();
                    rev  += buf_sample;
                }
                rev *= 0.25f; // normalise 4 combs

                // 2 allpass filters in series.
                for (size_t i = 0; i < NUM_ALLPASS; ++i)
                {
                    auto& d    = cl.allpass[i];
                    float buf_sample  = d.buf[d.pos];
                    float out_sample  = -rev + buf_sample;
                    d.buf[d.pos]      = rev + buf_sample * 0.5f;
                    d.pos = (d.pos + 1) % d.buf.size();
                    rev   = out_sample;
                }

                out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)] =
                    dry * x + wet_ * rev;
            }
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

    void set_params(float room_size, float damping, float wet)
    {
        room_size_ = room_size;
        damping_   = damping;
        wet_       = wet;
    }

private:
    float room_size_;
    float damping_;
    float wet_;

    std::unique_ptr<audio_buffer> buf_;

    static constexpr size_t NUM_COMBS   = 4;
    static constexpr size_t NUM_ALLPASS = 2;

    // Comb/allpass delay line base sizes in samples at 44100 Hz, per channel.
    // Second channel gets a small offset for stereo spread.
    static constexpr int COMB_SIZES[NUM_COMBS]     = {1317, 1637, 1813, 1931};
    static constexpr int ALLPASS_SIZES[NUM_ALLPASS] = {221,  75};
    static constexpr int STEREO_SPREAD              = 23;

    struct delay_line
    {
        std::vector<float> buf;
        size_t             pos {0};
        float              filter_store {0.f};
    };

    struct channel_lines
    {
        delay_line comb[NUM_COMBS];
        delay_line allpass[NUM_ALLPASS];
    };

    std::vector<channel_lines> ch_lines_;
    unsigned int               built_for_freq_ {0};
    int                        built_for_ch_   {0};

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }

    void ensure_delay_lines(audio_spec spec)
    {
        if (built_for_freq_ == spec.frequency && built_for_ch_ == spec.channels)
            return;

        const float scale = static_cast<float>(spec.frequency) / 44100.f;
        ch_lines_.resize(static_cast<size_t>(spec.channels));

        for (int c = 0; c < spec.channels; ++c)
        {
            const int spread = c * STEREO_SPREAD;
            auto& cl = ch_lines_[static_cast<size_t>(c)];
            for (size_t i = 0; i < NUM_COMBS; ++i)
            {
                size_t sz = static_cast<size_t>(COMB_SIZES[i] * scale) + static_cast<size_t>(spread);
                cl.comb[i].buf.assign(sz, 0.f);
                cl.comb[i].pos          = 0;
                cl.comb[i].filter_store = 0.f;
            }
            for (size_t i = 0; i < NUM_ALLPASS; ++i)
            {
                size_t sz = static_cast<size_t>(ALLPASS_SIZES[i] * scale) + static_cast<size_t>(spread);
                cl.allpass[i].buf.assign(sz, 0.f);
                cl.allpass[i].pos          = 0;
                cl.allpass[i].filter_store = 0.f;
            }
        }

        built_for_freq_ = spec.frequency;
        built_for_ch_   = spec.channels;
    }
};

// Dynamics compressor with optional auto-duck sidechain.
// Input 0: main signal. Input 1 (optional): sidechain — when present, its
// RMS drives the gain reduction applied to input 0 (auto-duck).
// All processing is in FLOAT interleaved format.
// Props: threshold_db, ratio, attack_ms, release_ms, makeup_db.
class compressor_node : public node
{
public:
    compressor_node(node_id id,
                    float threshold_db, float ratio,
                    float attack_ms,    float release_ms,
                    float makeup_db)
        : node(id)
        , threshold_db_(threshold_db), ratio_(ratio)
        , attack_ms_(attack_ms), release_ms_(release_ms)
        , makeup_db_(makeup_db)
    {}

    void set_params(float threshold_db, float ratio,
                    float attack_ms,    float release_ms,
                    float makeup_db)
    {
        threshold_db_ = threshold_db;
        ratio_        = ratio;
        attack_ms_    = attack_ms;
        release_ms_   = release_ms;
        makeup_db_    = makeup_db;
    }

    void process(audio_spec spec, size_t frames,
                 const std::vector<audio_buffer*>& inputs) override
    {
        assert(spec.format == audio_format::FLOAT);
        ensure_buf(spec, frames);

        const audio_buffer* main     = (inputs.size() > 0) ? inputs[0] : nullptr;
        const audio_buffer* sidechain= (inputs.size() > 1) ? inputs[1] : nullptr;

        float* out = reinterpret_cast<float*>(buf_->data().data());

        // Copy main signal into output (or silence if no input).
        if (main && main->frames() >= frames)
            std::memcpy(out, main->data().data(), frames * static_cast<size_t>(spec.channels) * sizeof(float));
        else
            std::fill(buf_->data().begin(), buf_->data().end(), std::byte{0});

        const int   ch        = spec.channels;
        const float sr        = static_cast<float>(spec.frequency);
        const float thresh_lin = db_to_lin(threshold_db_);
        const float makeup_lin = db_to_lin(makeup_db_);
        const float attack_coef  = std::exp(-1.f / (attack_ms_  * 0.001f * sr));
        const float release_coef = std::exp(-1.f / (release_ms_ * 0.001f * sr));

        for (size_t f = 0; f < frames; ++f)
        {
            // Compute detector signal: sidechain channel-0 or main channel-0.
            float det = 0.f;
            if (sidechain && sidechain->frames() > f)
            {
                const float* sc = reinterpret_cast<const float*>(sidechain->data().data());
                det = std::fabs(sc[f * static_cast<size_t>(ch)]);
            }
            else
            {
                det = std::fabs(out[f * static_cast<size_t>(ch)]);
            }

            // Envelope follower.
            float coef = (det > env_) ? attack_coef : release_coef;
            env_ = det + coef * (env_ - det);

            // Gain computer.
            float gain = 1.f;
            if (env_ > thresh_lin && ratio_ > 1.f)
            {
                float over_db  = lin_to_db(env_) - threshold_db_;
                float reduced  = over_db / ratio_;
                gain = db_to_lin(threshold_db_ + reduced) / env_;
            }
            gain *= makeup_lin;

            // Apply gain to all channels.
            for (int c = 0; c < ch; ++c)
                out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)] *= gain;
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    float threshold_db_;
    float ratio_;
    float attack_ms_;
    float release_ms_;
    float makeup_db_;

    float env_ {0.f};

    std::unique_ptr<audio_buffer> buf_;

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }

    static float db_to_lin(float db) { return std::pow(10.f, db / 20.f); }
    static float lin_to_db(float lin) { return 20.f * std::log10(lin + 1e-30f); }
};


// 5-band parametric EQ.
// Bands 0 and 4 are low/high shelves; bands 1-3 are peaking filters.
// All bands operate on FLOAT interleaved audio.
// Per-band props (prefix band0_ … band4_): freq_hz, gain_db, q.
class eq5_node : public node
{
public:
    struct band_params
    {
        float freq_hz {1000.f};
        float gain_db {0.f};
        float q       {0.707f};
    };

    explicit eq5_node(node_id id) : node(id) {}

    void set_band(size_t band, float freq_hz, float gain_db, float q)
    {
        assert(band < NUM_BANDS);
        params_[band] = {freq_hz, gain_db, q};
        dirty_ = true;
    }

    void process(audio_spec spec, size_t frames,
                 const std::vector<audio_buffer*>& inputs) override
    {
        assert(spec.format == audio_format::FLOAT);
        ensure_buf(spec, frames);

        const audio_buffer* src = (inputs.size() > 0) ? inputs[0] : nullptr;
        if (src && src->frames() >= frames)
            std::memcpy(buf_->data().data(), src->data().data(),
                        frames * static_cast<size_t>(spec.channels) * sizeof(float));
        else
            std::fill(buf_->data().begin(), buf_->data().end(), std::byte{0});

        rebuild_coeffs(spec);

        float* out  = reinterpret_cast<float*>(buf_->data().data());
        const int ch = spec.channels;

        for (size_t f = 0; f < frames; ++f)
        {
            for (int c = 0; c < ch; ++c)
            {
                float x = out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                for (size_t b = 0; b < NUM_BANDS; ++b)
                    x = biquad_process(b, c, x);
                out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)] = x;
            }
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    static constexpr size_t NUM_BANDS = 5;

    band_params params_[NUM_BANDS] = {
        {80.f,   0.f, 0.707f},
        {250.f,  0.f, 0.707f},
        {1000.f, 0.f, 0.707f},
        {4000.f, 0.f, 0.707f},
        {12000.f,0.f, 0.707f},
    };

    // Biquad coefficients per band.
    struct biquad_coeffs { float b0, b1, b2, a1, a2; };
    biquad_coeffs coeffs_[NUM_BANDS] {};

    // Per-band per-channel state (x1, x2, y1, y2).
    struct biquad_state { float x1{}, x2{}, y1{}, y2{}; };
    // state_[band][channel]
    std::vector<std::array<biquad_state, NUM_BANDS>> ch_state_; // indexed [ch][band]

    std::unique_ptr<audio_buffer> buf_;
    unsigned int built_for_freq_ {0};
    int          built_for_ch_   {0};
    bool         dirty_          {true};

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }

    void rebuild_coeffs(audio_spec spec)
    {
        if (!dirty_ && built_for_freq_ == spec.frequency && built_for_ch_ == spec.channels)
            return;

        const float sr = static_cast<float>(spec.frequency);

        for (size_t b = 0; b < NUM_BANDS; ++b)
        {
            const float f0 = params_[b].freq_hz;
            const float g  = params_[b].gain_db;
            const float q  = std::max(params_[b].q, 0.01f);
            const float A  = std::pow(10.f, g / 40.f);
            const float w0 = 2.f * 3.14159265f * f0 / sr;
            const float cw = std::cos(w0);
            const float sw = std::sin(w0);
            const float alpha = sw / (2.f * q);

            biquad_coeffs& c = coeffs_[b];

            if (b == 0)
            {
                // Low shelf
                const float sqA = std::sqrt(A);
                float b0 =  A * ((A + 1) - (A - 1)*cw + 2*sqA*alpha);
                float b1 =  2*A * ((A - 1) - (A + 1)*cw);
                float b2 =  A * ((A + 1) - (A - 1)*cw - 2*sqA*alpha);
                float a0 =       (A + 1) + (A - 1)*cw + 2*sqA*alpha;
                float a1 = -2  * ((A - 1) + (A + 1)*cw);
                float a2 =       (A + 1) + (A - 1)*cw - 2*sqA*alpha;
                c = {b0/a0, b1/a0, b2/a0, a1/a0, a2/a0};
            }
            else if (b == NUM_BANDS - 1)
            {
                // High shelf
                const float sqA = std::sqrt(A);
                float b0 =  A * ((A + 1) + (A - 1)*cw + 2*sqA*alpha);
                float b1 = -2*A * ((A - 1) + (A + 1)*cw);
                float b2 =  A * ((A + 1) + (A - 1)*cw - 2*sqA*alpha);
                float a0 =       (A + 1) - (A - 1)*cw + 2*sqA*alpha;
                float a1 =  2  * ((A - 1) - (A + 1)*cw);
                float a2 =       (A + 1) - (A - 1)*cw - 2*sqA*alpha;
                c = {b0/a0, b1/a0, b2/a0, a1/a0, a2/a0};
            }
            else
            {
                // Peaking EQ
                float b0 =  1 + alpha * A;
                float b1 = -2 * cw;
                float b2 =  1 - alpha * A;
                float a0 =  1 + alpha / A;
                float a1 = -2 * cw;
                float a2 =  1 - alpha / A;
                c = {b0/a0, b1/a0, b2/a0, a1/a0, a2/a0};
            }
        }

        // Resize state array if channel count changed.
        if (built_for_ch_ != spec.channels)
        {
            ch_state_.assign(static_cast<size_t>(spec.channels), {});
        }

        built_for_freq_ = spec.frequency;
        built_for_ch_   = spec.channels;
        dirty_          = false;
    }

    float biquad_process(size_t band, int ch, float x)
    {
        auto& s = ch_state_[static_cast<size_t>(ch)][band];
        const auto& c = coeffs_[band];
        float y = c.b0*x + c.b1*s.x1 + c.b2*s.x2 - c.a1*s.y1 - c.a2*s.y2;
        s.x2 = s.x1; s.x1 = x;
        s.y2 = s.y1; s.y1 = y;
        return y;
    }
};

// Simple gain node — multiplies all samples by a linear gain derived from gain_db.
// Passes through FLOAT interleaved audio unchanged in structure.
class gain_node : public node
{
public:
    gain_node(node_id id, float gain_db)
        : node(id), gain_db_(gain_db) {}

    void set_gain_db(float gain_db) { gain_db_ = gain_db; }

    void process(audio_spec spec, size_t frames,
                 const std::vector<audio_buffer*>& inputs) override
    {
        assert(spec.format == audio_format::FLOAT);
        ensure_buf(spec, frames);

        const audio_buffer* src = inputs.size() > 0 ? inputs[0] : nullptr;
        if (src && src->frames() >= frames)
            std::memcpy(buf_->data().data(), src->data().data(),
                        frames * static_cast<size_t>(spec.channels) * sizeof(float));
        else
            std::fill(buf_->data().begin(), buf_->data().end(), std::byte{0});

        const float gain  = std::pow(10.f, gain_db_ / 20.f);
        float* out        = reinterpret_cast<float*>(buf_->data().data());
        const size_t count = frames * static_cast<size_t>(spec.channels);
        for (size_t i = 0; i < count; ++i)
            out[i] *= gain;
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    float gain_db_;
    std::unique_ptr<audio_buffer> buf_;

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }
};

} // namespace nb::audio_graph
