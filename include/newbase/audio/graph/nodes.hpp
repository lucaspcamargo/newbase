#pragma once

#include <newbase/audio/graph/node.hpp>
#include <newbase/audio/producer.hpp>
#include <newbase/audio/converter.hpp>
#include <newbase/audio/visualizer_feedback.hpp>
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
        log::verb("[output_node %d] processing NUM_INPUTS=%zu frames=%zu", id(), inputs.size(), frames);
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
            log::verb("[source_node %d] direct pull, frames=%zu", id(), frames);
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

// Bitcrusher: reduces bit depth (quantisation) and/or sample rate (sample-and-hold).
// bits:       1–32, number of bits to quantise to (lower = more grit).
// downsample: 1–32, hold each sample N times (integer decimation, no filtering).
// Both operate on FLOAT interleaved audio.
class bitcrusher_node : public node
{
public:
    bitcrusher_node(node_id id, float bits, float downsample)
        : node(id), bits_(bits), downsample_(downsample) {}

    void set_params(float bits, float downsample)
    {
        bits_       = bits;
        downsample_ = downsample;
    }

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

        float* out = reinterpret_cast<float*>(buf_->data().data());
        const int ch = spec.channels;
        const size_t total = frames * static_cast<size_t>(ch);

        // Quantisation: map [-1,1] to N-bit integer steps and back.
        const float levels = std::pow(2.f, std::max(1.f, std::min(bits_, 32.f))) - 1.f;
        const int   ds     = std::max(1, static_cast<int>(downsample_));

        // Per-channel held sample for sample-rate reduction.
        if (static_cast<int>(hold_.size()) < ch)
            hold_.assign(static_cast<size_t>(ch), 0.f);

        for (size_t f = 0; f < frames; ++f)
        {
            for (int c = 0; c < ch; ++c)
            {
                float& s = out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                // Sample-and-hold: only update held value every ds frames.
                if ((static_cast<int>(f) % ds) == 0)
                    hold_[static_cast<size_t>(c)] = std::round(s * levels) / levels;
                s = hold_[static_cast<size_t>(c)];
            }
        }
        (void)total;
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    float bits_;
    float downsample_;
    std::unique_ptr<audio_buffer> buf_;
    std::vector<float>            hold_; // per-channel held sample

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
        {
            buf_  = std::make_unique<audio_buffer>(spec, frames, nullptr);
            hold_.clear(); // rebuild hold state for new channel count
        }
    }
};

// Delay line with feedback.
// delay_ms: delay time in milliseconds.
// feedback: [0,1) — fraction of output fed back into the delay line.
// mix:      [0,1] — wet/dry blend (0 = dry, 1 = fully wet).
class delay_node : public node
{
public:
    delay_node(node_id id, float delay_ms, float feedback, float mix)
        : node(id), delay_ms_(delay_ms), feedback_(feedback), mix_(mix) {}

    void set_params(float delay_ms, float feedback, float mix)
    {
        delay_ms_ = delay_ms;
        feedback_ = feedback;
        mix_      = mix;
        // Rebuild delay buffers if time changed significantly.
        built_for_freq_ = 0;
    }

    void process(audio_spec spec, size_t frames,
                 const std::vector<audio_buffer*>& inputs) override
    {
        assert(spec.format == audio_format::FLOAT);
        ensure_buf(spec, frames);
        ensure_delay(spec);

        const audio_buffer* src = inputs.size() > 0 ? inputs[0] : nullptr;
        if (src && src->frames() >= frames)
            std::memcpy(buf_->data().data(), src->data().data(),
                        frames * static_cast<size_t>(spec.channels) * sizeof(float));
        else
            std::fill(buf_->data().begin(), buf_->data().end(), std::byte{0});

        float*     out = reinterpret_cast<float*>(buf_->data().data());
        const int  ch  = spec.channels;
        const float fb  = std::min(std::max(feedback_, 0.f), 0.99f);
        const float wet = std::min(std::max(mix_, 0.f), 1.f);
        const float dry = 1.f - wet;

        for (size_t f = 0; f < frames; ++f)
        {
            for (int c = 0; c < ch; ++c)
            {
                auto& line = lines_[static_cast<size_t>(c)];
                float in_s  = out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                float delayed = line.buf[line.pos];
                line.buf[line.pos] = in_s + delayed * fb;
                line.pos = (line.pos + 1) % line.buf.size();
                out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)] =
                    dry * in_s + wet * delayed;
            }
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    float delay_ms_;
    float feedback_;
    float mix_;

    struct delay_line { std::vector<float> buf; size_t pos {0}; };
    std::vector<delay_line> lines_;
    unsigned int built_for_freq_ {0};
    int          built_for_ch_   {0};

    std::unique_ptr<audio_buffer> buf_;

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }

    void ensure_delay(audio_spec spec)
    {
        static constexpr float MAX_DELAY_MS = 5000.f;
        const float clamped_ms = std::max(1.f, std::min(delay_ms_, MAX_DELAY_MS));
        const size_t delay_samples =
            static_cast<size_t>(clamped_ms * 0.001f * static_cast<float>(spec.frequency));
        if (built_for_freq_ == spec.frequency
            && built_for_ch_  == spec.channels
            && !lines_.empty()
            && lines_[0].buf.size() == delay_samples)
            return;
        lines_.resize(static_cast<size_t>(spec.channels));
        for (auto& l : lines_) { l.buf.assign(delay_samples, 0.f); l.pos = 0; }
        built_for_freq_ = spec.frequency;
        built_for_ch_   = spec.channels;
    }
};


// Ring modulator: multiplies the input by a sine carrier.
// carrier_hz: frequency of the carrier sine wave.
// mix:        [0,1] wet/dry blend.
class ring_mod_node : public node
{
public:
    ring_mod_node(node_id id, float carrier_hz, float mix)
        : node(id), carrier_hz_(carrier_hz), mix_(mix) {}

    void set_params(float carrier_hz, float mix)
    {
        carrier_hz_ = carrier_hz;
        mix_        = mix;
    }

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

        float*      out  = reinterpret_cast<float*>(buf_->data().data());
        const int   ch   = spec.channels;
        const float sr   = static_cast<float>(spec.frequency);
        const float wet  = std::min(std::max(mix_, 0.f), 1.f);
        const float dry  = 1.f - wet;
        const float step = 2.f * 3.14159265f * carrier_hz_ / sr;

        for (size_t f = 0; f < frames; ++f)
        {
            const float carrier = std::sin(phase_);
            phase_ += step;
            if (phase_ > 2.f * 3.14159265f) phase_ -= 2.f * 3.14159265f;
            for (int c = 0; c < ch; ++c)
            {
                float& s = out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                s = dry * s + wet * s * carrier;
            }
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    float carrier_hz_;
    float mix_;
    float phase_ {0.f};
    std::unique_ptr<audio_buffer> buf_;

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }
};


// 1-input 1-output pass-through node that captures float samples into a
// visualizer_feedback struct for waveform display on the main thread.
class visualizer_node : public node
{
public:
    explicit visualizer_node(node_id id) : node(id) {}

    void set_feedback(std::shared_ptr<nb::visualizer_feedback> fb) { fb_ = std::move(fb); }

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

        if (fb_)
        {
            const float* ptr = reinterpret_cast<const float*>(buf_->data().data());
            fb_->push_samples(ptr, spec.channels, frames);
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    std::unique_ptr<audio_buffer>               buf_;
    std::shared_ptr<nb::visualizer_feedback>    fb_;

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }
};

// Chorus: N voices, each a short LFO-modulated delay tap, summed with dry.
// rate_hz:  LFO speed (0.01–10 Hz).
// depth_ms: maximum LFO delay modulation in milliseconds (1–30).
// voices:   number of delay taps (1–4); each is phase-offset evenly.
// mix:      wet/dry blend [0,1].
class chorus_node : public node
{
public:
    chorus_node(node_id id, float rate_hz, float depth_ms, float voices, float mix)
        : node(id), rate_hz_(rate_hz), depth_ms_(depth_ms)
        , voices_(voices), mix_(mix) {}

    void set_params(float rate_hz, float depth_ms, float voices, float mix)
    {
        rate_hz_  = rate_hz;
        depth_ms_ = depth_ms;
        voices_   = voices;
        mix_      = mix;
        built_for_freq_ = 0; // force delay buffer rebuild
    }

    void process(audio_spec spec, size_t frames,
                 const std::vector<audio_buffer*>& inputs) override
    {
        assert(spec.format == audio_format::FLOAT);
        ensure_buf(spec, frames);
        ensure_lines(spec);

        const audio_buffer* src = inputs.size() > 0 ? inputs[0] : nullptr;
        if (src && src->frames() >= frames)
            std::memcpy(buf_->data().data(), src->data().data(),
                        frames * static_cast<size_t>(spec.channels) * sizeof(float));
        else
            std::fill(buf_->data().begin(), buf_->data().end(), std::byte{0});

        float*     out  = reinterpret_cast<float*>(buf_->data().data());
        const int  ch   = spec.channels;
        const float sr  = static_cast<float>(spec.frequency);
        const float wet = std::min(std::max(mix_, 0.f), 1.f);
        const float dry = 1.f - wet;
        const int   nv  = std::max(1, std::min(static_cast<int>(voices_), 4));
        const float step = 2.f * 3.14159265f * rate_hz_ / sr;
        // max delay in samples; base delay is half that so the LFO swings both ways
        const float max_delay_smp = depth_ms_ * 0.001f * sr;
        const float base_delay    = max_delay_smp * 0.5f + 1.f;

        for (size_t f = 0; f < frames; ++f)
        {
            for (int c = 0; c < ch; ++c)
            {
                const float dry_s = out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                float wet_s = 0.f;

                for (int v = 0; v < nv; ++v)
                {
                    // Each voice has an evenly spread LFO phase offset.
                    const float voice_phase = lfo_phase_
                        + static_cast<float>(v) * (2.f * 3.14159265f / static_cast<float>(nv));
                    const float lfo = std::sin(voice_phase);
                    const float delay_smp = base_delay + lfo * max_delay_smp * 0.5f;

                    auto& line = lines_[static_cast<size_t>(v * ch + c)];
                    // Write current sample into the circular buffer.
                    line.buf[line.write_pos] = dry_s;
                    // Read back with fractional delay (linear interpolation).
                    const float read_f = static_cast<float>(line.write_pos) - delay_smp;
                    const size_t sz = line.buf.size();
                    auto wrap = [sz](float p) -> size_t {
                        long ip = static_cast<long>(p);
                        ip = ((ip % static_cast<long>(sz)) + static_cast<long>(sz)) % static_cast<long>(sz);
                        return static_cast<size_t>(ip);
                    };
                    const size_t r0 = wrap(std::floor(read_f));
                    const size_t r1 = wrap(std::floor(read_f) + 1.f);
                    const float  t  = read_f - std::floor(read_f);
                    wet_s += line.buf[r0] * (1.f - t) + line.buf[r1] * t;
                    line.write_pos = (line.write_pos + 1) % sz;
                }
                wet_s /= static_cast<float>(nv);
                out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)] =
                    dry * dry_s + wet * wet_s;
            }
            lfo_phase_ += step;
            if (lfo_phase_ > 2.f * 3.14159265f) lfo_phase_ -= 2.f * 3.14159265f;
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    float rate_hz_;
    float depth_ms_;
    float voices_;
    float mix_;
    float lfo_phase_ {0.f};

    struct circ_line { std::vector<float> buf; size_t write_pos {0}; };
    std::vector<circ_line> lines_; // [voice * ch + ch]
    unsigned int built_for_freq_ {0};
    int          built_for_ch_   {0};
    std::unique_ptr<audio_buffer> buf_;

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }

    void ensure_lines(audio_spec spec)
    {
        const int nv = std::max(1, std::min(static_cast<int>(voices_), 4));
        if (built_for_freq_ == spec.frequency && built_for_ch_ == spec.channels
            && static_cast<int>(lines_.size()) == nv * spec.channels)
            return;
        // +4 samples headroom beyond maximum possible delay
        const size_t max_smp = static_cast<size_t>(depth_ms_ * 0.001f * static_cast<float>(spec.frequency)) + 4;
        lines_.assign(static_cast<size_t>(nv * spec.channels), circ_line{});
        for (auto& l : lines_) { l.buf.assign(max_smp, 0.f); l.write_pos = 0; }
        built_for_freq_ = spec.frequency;
        built_for_ch_   = spec.channels;
    }
};


// Waveshaper: non-linear distortion via drive + selectable shape curve.
// drive: input gain before shaping [1, 100].
// shape: 0 = soft clip (tanh), 1 = hard clip, 2 = fold-back.
// mix:   wet/dry blend [0,1].
class waveshaper_node : public node
{
public:
    waveshaper_node(node_id id, float drive, float shape, float mix)
        : node(id), drive_(drive), shape_(shape), mix_(mix) {}

    void set_params(float drive, float shape, float mix)
    {
        drive_ = drive;
        shape_ = shape;
        mix_   = mix;
    }

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

        float*      out  = reinterpret_cast<float*>(buf_->data().data());
        const size_t cnt = frames * static_cast<size_t>(spec.channels);
        const float wet  = std::min(std::max(mix_,   0.f), 1.f);
        const float dry  = 1.f - wet;
        const float drv  = std::max(1.f, drive_);
        // normalise output by drive so perceived loudness stays roughly constant
        const float norm = 1.f / std::tanh(drv);
        const int   mode = static_cast<int>(std::round(std::min(std::max(shape_, 0.f), 2.f)));

        for (size_t i = 0; i < cnt; ++i)
        {
            const float in_s = out[i];
            float shaped;
            switch (mode)
            {
                case 1: // hard clip
                {
                    float x = in_s * drv;
                    shaped  = x < -1.f ? -1.f : (x > 1.f ? 1.f : x);
                    break;
                }
                case 2: // fold-back
                {
                    float x = in_s * drv;
                    // fold: if |x|>1 reflect back
                    while (x >  1.f) x =  2.f - x;
                    while (x < -1.f) x = -2.f - x;
                    shaped = x;
                    break;
                }
                default: // soft clip (tanh)
                    shaped = std::tanh(in_s * drv) * norm;
                    break;
            }
            out[i] = dry * in_s + wet * shaped;
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    float drive_;
    float shape_;
    float mix_;
    std::unique_ptr<audio_buffer> buf_;

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }
};


// Phaser: cascade of N all-pass filters whose centre frequency is swept by an LFO.
// Creates notch patterns that move through the spectrum.
// rate_hz:  LFO rate [0.01, 10].
// depth:    LFO modulation depth [0, 1].
// stages:   number of all-pass stages (2 / 4 / 6 / 8).
// feedback: fraction of output fed back to input [0, 0.99].
// mix:      wet/dry blend [0, 1].
class phaser_node : public node
{
public:
    phaser_node(node_id id, float rate_hz, float depth, float stages, float feedback, float mix)
        : node(id), rate_hz_(rate_hz), depth_(depth)
        , stages_(stages), feedback_(feedback), mix_(mix) {}

    void set_params(float rate_hz, float depth, float stages, float feedback, float mix)
    {
        rate_hz_  = rate_hz;
        depth_    = depth;
        stages_   = stages;
        feedback_ = feedback;
        mix_      = mix;
        dirty_    = true;
    }

    void process(audio_spec spec, size_t frames,
                 const std::vector<audio_buffer*>& inputs) override
    {
        assert(spec.format == audio_format::FLOAT);
        ensure_buf(spec, frames);
        ensure_state(spec);

        const audio_buffer* src = inputs.size() > 0 ? inputs[0] : nullptr;
        if (src && src->frames() >= frames)
            std::memcpy(buf_->data().data(), src->data().data(),
                        frames * static_cast<size_t>(spec.channels) * sizeof(float));
        else
            std::fill(buf_->data().begin(), buf_->data().end(), std::byte{0});

        float*      out  = reinterpret_cast<float*>(buf_->data().data());
        const int   ch   = spec.channels;
        const float sr   = static_cast<float>(spec.frequency);
        const float wet  = std::min(std::max(mix_,      0.f), 1.f);
        const float dry  = 1.f - wet;
        const float fb   = std::min(std::max(feedback_, 0.f), 0.99f);
        const float step = 2.f * 3.14159265f * rate_hz_ / sr;
        const int   nst  = num_stages();

        // LFO modulates the all-pass coefficient 'a' in [min_a, max_a].
        // a = (tan(π·fc/sr) - 1) / (tan(π·fc/sr) + 1)  — first-order all-pass
        // We sweep fc between base_hz and base_hz + depth·(sr/2 - base_hz).
        const float base_hz  = 200.f;
        const float top_hz   = sr * 0.45f;
        const float range_hz = (top_hz - base_hz) * std::min(std::max(depth_, 0.f), 1.f);

        for (size_t f = 0; f < frames; ++f)
        {
            const float lfo  = 0.5f * (1.f + std::sin(lfo_phase_));  // [0,1]
            const float fc   = base_hz + lfo * range_hz;
            const float tanw = std::tan(3.14159265f * fc / sr);
            const float a    = (tanw - 1.f) / (tanw + 1.f);

            for (int c = 0; c < ch; ++c)
            {
                float x = out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                // add feedback from previous output
                x += fb * fb_state_[static_cast<size_t>(c)];

                float y = x;
                for (int s = 0; s < nst; ++s)
                {
                    auto& st = ap_state_[static_cast<size_t>(c * MAX_STAGES + s)];
                    // y[n] = a*(x[n] - y[n-1]) + x[n-1]
                    float yn = a * (y - st.y1) + st.x1;
                    st.x1 = y;
                    st.y1 = yn;
                    y = yn;
                }
                fb_state_[static_cast<size_t>(c)] = y;
                out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)] =
                    dry * x + wet * y;
            }

            lfo_phase_ += step;
            if (lfo_phase_ > 2.f * 3.14159265f) lfo_phase_ -= 2.f * 3.14159265f;
        }
    }

    audio_buffer* output_buffer() override { return buf_.get(); }

private:
    float rate_hz_;
    float depth_;
    float stages_;
    float feedback_;
    float mix_;
    bool  dirty_     {true};
    float lfo_phase_ {0.f};

    static constexpr int MAX_STAGES = 8;

    struct ap_state { float x1 {0.f}, y1 {0.f}; };
    // ap_state_[ch * MAX_STAGES + stage]
    std::vector<ap_state> ap_state_;
    std::vector<float>    fb_state_; // per-channel feedback

    std::unique_ptr<audio_buffer> buf_;
    int built_for_ch_ {0};

    int num_stages() const
    {
        // round to nearest even in {2,4,6,8}
        int n = static_cast<int>(std::round(stages_));
        n = n < 2 ? 2 : (n > MAX_STAGES ? MAX_STAGES : n);
        return (n + 1) & ~1; // round up to even
    }

    void ensure_buf(audio_spec spec, size_t frames)
    {
        if (!buf_ || buf_->spec() != spec || buf_->frames() != frames)
            buf_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }

    void ensure_state(audio_spec spec)
    {
        if (built_for_ch_ == spec.channels && !dirty_) return;
        ap_state_.assign(static_cast<size_t>(spec.channels * MAX_STAGES), ap_state{});
        fb_state_.assign(static_cast<size_t>(spec.channels), 0.f);
        built_for_ch_ = spec.channels;
        dirty_        = false;
    }
};

} // namespace nb::audio_graph
