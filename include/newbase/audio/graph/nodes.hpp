#pragma once

#include <newbase/nb_config.h>
#include <newbase/audio/dsp_utils.hpp>
#include <newbase/audio/graph/node.hpp>
#include <newbase/audio/producer.hpp>
#include <newbase/audio/converter.hpp>
#include <newbase/audio/visualizer_feedback.hpp>
#include <newbase/audio/producer/lpc.hpp>
#include <newbase/audio/lpc_vocab.hpp>
#include <newbase/log.hpp>
#include <memory>
#include <cassert>
#include <cstring>
#include <cmath>
#include <vector>
#ifdef NEWBASE_TALKIE_PCM
#include <cstdio>
#include <cstdlib>
#include <cctype>
// dtostrf is missing outside Arduino; provide it so TalkiePCM.h compiles on desktop
static inline char* dtostrf(double val, signed char /*width*/, unsigned char prec, char* buf)
{
    std::snprintf(buf, 14, "%.*f", static_cast<int>(prec), val);
    return buf;
}
#include <TalkiePCM.h>
#include <mutex>
#include <newbase/audio/talkie_pcm_vocab.hpp>
#endif

namespace nb::audio_graph
{

// Add src samples into dst (float format only; asserts otherwise).
inline void mix_add(audio_buffer::span& dst, const audio_buffer::span& src)
{
    assert(dst.buffer_ref().spec().format == audio_format::FLOAT);
    assert(src.buffer_ref().spec().format == audio_format::FLOAT);
    assert(dst.buffer_ref().spec().channels == src.buffer_ref().spec().channels);
    float*       d     = reinterpret_cast<float*>(dst.begin());
    const float* s     = reinterpret_cast<const float*>(src.begin());
    const size_t count = dst.frames() * static_cast<size_t>(dst.buffer_ref().channels());
    for (size_t i = 0; i < count; ++i)
        d[i] += s[i];
}


// Final audio sink — mixes all inputs into one buffer (always node id=0).
class output_node : public node
{
public:
    explicit output_node(node_id id) : node(id) {}

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        log::verb("[output_node %d] pull inputs=%zu frames=%zu", id(), inputs_.size(), dst.frames());
        std::fill(dst.begin(), dst.end(), std::byte{0});
        for (size_t i = 0; i < inputs_.size(); ++i)
        {
            if (i == 0)
            {
                inputs_[0]->pull(dst, gen);
            }
            else
            {
                ensure_scratch(dst.buffer_ref().spec(), dst.frames());
                std::fill(scratch_->data().begin(), scratch_->data().end(), std::byte{0});
                auto scratch_span = scratch_->as_span();
                inputs_[i]->pull(scratch_span, gen);
                mix_add(dst, scratch_span);
            }
        }
    }

private:
    std::unique_ptr<audio_buffer> scratch_;

    void ensure_scratch(audio_spec spec, size_t frames)
    {
        if (!scratch_ || scratch_->spec() != spec || scratch_->frames() != frames)
            scratch_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
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

    void pull(audio_spec graph_spec, size_t frames, audio_buffer::span& dst)
    {
        std::fill(dst.begin(), dst.end(), std::byte{0});

        if (!producer_)
            return;

        const audio_spec prod_spec = producer_->spec();

        if (prod_spec == graph_spec)
        {
            log::verb("[source_node %d] direct pull, frames=%zu", id(), frames);
            producer_->frames_pull(audio_buffer::span(dst), frames);
        }
        else
        {
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
                producer_->frames_pull(audio_buffer::span(scratch_buf_->as_span()), input_frames);
                auto scratch_sp = scratch_buf_->as_span();
                converter_->put(audio_buffer::span(scratch_sp));
            }
            converter_->take(audio_buffer::span(dst));
        }
    }

    void pull(audio_buffer::span& dst, uint64_t /*gen*/) override
    {
        pull(dst.buffer_ref().spec(), dst.frames(), dst);
    }

    audio_producer* producer() { return producer_.get(); }
    void set_producer(std::unique_ptr<audio_producer> p)
    {
        producer_ = std::move(p);
        converter_.reset();
    }

private:
    std::unique_ptr<audio_producer>  producer_;
    std::unique_ptr<audio_buffer>    scratch_buf_;
    std::unique_ptr<audio_converter> converter_;
    audio_spec                       converter_in_spec_;
    audio_spec                       converter_out_spec_;

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

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        std::fill(dst.begin(), dst.end(), std::byte{0});
        for (size_t i = 0; i < inputs_.size(); ++i)
        {
            if (i == 0)
            {
                inputs_[0]->pull(dst, gen);
            }
            else
            {
                ensure_scratch(dst.buffer_ref().spec(), dst.frames());
                std::fill(scratch_->data().begin(), scratch_->data().end(), std::byte{0});
                auto scratch_span = scratch_->as_span();
                inputs_[i]->pull(scratch_span, gen);
                mix_add(dst, scratch_span);
            }
        }
    }

private:
    std::unique_ptr<audio_buffer> scratch_;

    void ensure_scratch(audio_spec spec, size_t frames)
    {
        if (!scratch_ || scratch_->spec() != spec || scratch_->frames() != frames)
            scratch_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
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

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (!inputs_.empty())
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        const audio_spec spec = dst.buffer_ref().spec();
        assert(spec.format == audio_format::FLOAT);
        const size_t frames = dst.frames();
        const int    ch     = spec.channels;
        float*       out    = reinterpret_cast<float*>(dst.begin());
        const float  dry    = 1.f - wet_;

        ensure_delay_lines(spec);

        for (int c = 0; c < ch; ++c)
        {
            auto& cl = ch_lines_[static_cast<size_t>(c)];
            for (size_t f = 0; f < frames; ++f)
            {
                float x = out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                float rev = 0.f;

                for (size_t i = 0; i < NUM_COMBS; ++i)
                {
                    auto& d   = cl.comb[i];
                    float buf_sample = d.buf[d.pos];
                    d.filter_store = dsp::kill_denorm(buf_sample * (1.f - damping_) + d.filter_store * damping_);
                    d.buf[d.pos]   = x + d.filter_store * room_size_;
                    d.pos = (d.pos + 1) % d.buf.size();
                    rev  += buf_sample;
                }
                rev *= 0.25f;

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

    static constexpr size_t NUM_COMBS   = 4;
    static constexpr size_t NUM_ALLPASS = 2;

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
// Input 0: main signal. Input 1 (optional): sidechain.
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

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        const audio_spec spec = dst.buffer_ref().spec();
        assert(spec.format == audio_format::FLOAT);

        // Pull main signal into dst.
        if (inputs_.size() > 0)
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        // Pull optional sidechain into scratch.
        const float* sidechain_ptr = nullptr;
        if (inputs_.size() > 1)
        {
            ensure_scratch(spec, dst.frames());
            std::fill(scratch_->data().begin(), scratch_->data().end(), std::byte{0});
            auto sc_span = scratch_->as_span();
            inputs_[1]->pull(sc_span, gen);
            sidechain_ptr = reinterpret_cast<const float*>(scratch_->data().data());
        }

        const size_t frames = dst.frames();
        float* out = reinterpret_cast<float*>(dst.begin());
        const int   ch        = spec.channels;
        const float sr        = static_cast<float>(spec.frequency);
        const float thresh_lin = db_to_lin(threshold_db_);
        const float makeup_lin = db_to_lin(makeup_db_);
        const float attack_coef  = std::exp(-1.f / (attack_ms_  * 0.001f * sr));
        const float release_coef = std::exp(-1.f / (release_ms_ * 0.001f * sr));

        for (size_t f = 0; f < frames; ++f)
        {
            float det = 0.f;
            if (sidechain_ptr)
                det = std::fabs(sidechain_ptr[f * static_cast<size_t>(ch)]);
            else
                det = std::fabs(out[f * static_cast<size_t>(ch)]);

            float coef = (det > env_) ? attack_coef : release_coef;
            env_ = det + coef * (env_ - det);

            float gain = 1.f;
            if (env_ > thresh_lin && ratio_ > 1.f)
            {
                float over_db  = lin_to_db(env_) - threshold_db_;
                float reduced  = over_db / ratio_;
                gain = db_to_lin(threshold_db_ + reduced) / env_;
            }
            gain *= makeup_lin;

            for (int c = 0; c < ch; ++c)
                out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)] *= gain;
        }
    }

private:
    float threshold_db_;
    float ratio_;
    float attack_ms_;
    float release_ms_;
    float makeup_db_;
    float env_ {0.f};

    std::unique_ptr<audio_buffer> scratch_;

    void ensure_scratch(audio_spec spec, size_t frames)
    {
        if (!scratch_ || scratch_->spec() != spec || scratch_->frames() != frames)
            scratch_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }

    static float db_to_lin(float db) { return std::pow(10.f, db / 20.f); }
    static float lin_to_db(float lin) { return 20.f * std::log10(lin + 1e-30f); }
};


// 5-band parametric EQ.
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

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (!inputs_.empty())
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        const audio_spec spec = dst.buffer_ref().spec();
        assert(spec.format == audio_format::FLOAT);
        const size_t frames = dst.frames();
        float* out  = reinterpret_cast<float*>(dst.begin());
        const int ch = spec.channels;

        rebuild_coeffs(spec);

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

private:
    static constexpr size_t NUM_BANDS = 5;

    band_params params_[NUM_BANDS] = {
        {80.f,   0.f, 0.707f},
        {250.f,  0.f, 0.707f},
        {1000.f, 0.f, 0.707f},
        {4000.f, 0.f, 0.707f},
        {12000.f,0.f, 0.707f},
    };

    struct biquad_coeffs { float b0, b1, b2, a1, a2; };
    biquad_coeffs coeffs_[NUM_BANDS] {};

    struct biquad_state { float x1{}, x2{}, y1{}, y2{}; };
    std::vector<std::array<biquad_state, NUM_BANDS>> ch_state_;

    unsigned int built_for_freq_ {0};
    int          built_for_ch_   {0};
    bool         dirty_          {true};

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
                float b0 =  1 + alpha * A;
                float b1 = -2 * cw;
                float b2 =  1 - alpha * A;
                float a0 =  1 + alpha / A;
                float a1 = -2 * cw;
                float a2 =  1 - alpha / A;
                c = {b0/a0, b1/a0, b2/a0, a1/a0, a2/a0};
            }
        }

        if (built_for_ch_ != spec.channels)
            ch_state_.assign(static_cast<size_t>(spec.channels), {});

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
        s.y2 = s.y1; s.y1 = dsp::kill_denorm(y);
        return y;
    }
};


// Simple gain node.
class gain_node : public node
{
public:
    gain_node(node_id id, float gain_db)
        : node(id), gain_db_(gain_db) {}

    void set_gain_db(float gain_db) { gain_db_ = gain_db; }

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (!inputs_.empty())
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        assert(dst.buffer_ref().spec().format == audio_format::FLOAT);
        const float gain   = std::pow(10.f, gain_db_ / 20.f);
        float*      out    = reinterpret_cast<float*>(dst.begin());
        const size_t count = dst.frames() * static_cast<size_t>(dst.buffer_ref().channels());
        for (size_t i = 0; i < count; ++i)
            out[i] *= gain;
    }

private:
    float gain_db_;
};


// Bitcrusher: reduces bit depth and/or sample rate.
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

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (!inputs_.empty())
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        const audio_spec spec = dst.buffer_ref().spec();
        assert(spec.format == audio_format::FLOAT);
        const size_t frames = dst.frames();
        float* out = reinterpret_cast<float*>(dst.begin());
        const int ch = spec.channels;

        const float levels = std::pow(2.f, std::max(1.f, std::min(bits_, 32.f))) - 1.f;
        const int   ds     = std::max(1, static_cast<int>(downsample_));

        if (static_cast<int>(hold_.size()) < ch)
            hold_.assign(static_cast<size_t>(ch), 0.f);

        for (size_t f = 0; f < frames; ++f)
        {
            for (int c = 0; c < ch; ++c)
            {
                float& s = out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                if ((static_cast<int>(f) % ds) == 0)
                    hold_[static_cast<size_t>(c)] = std::round(s * levels) / levels;
                s = hold_[static_cast<size_t>(c)];
            }
        }
    }

private:
    float bits_;
    float downsample_;
    std::vector<float> hold_;
};


// Delay line with feedback.
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
        built_for_freq_ = 0;
    }

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (!inputs_.empty())
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        const audio_spec spec = dst.buffer_ref().spec();
        assert(spec.format == audio_format::FLOAT);
        const size_t frames = dst.frames();
        ensure_delay(spec);

        float*     out = reinterpret_cast<float*>(dst.begin());
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

private:
    float delay_ms_;
    float feedback_;
    float mix_;

    struct delay_line { std::vector<float> buf; size_t pos {0}; };
    std::vector<delay_line> lines_;
    unsigned int built_for_freq_ {0};
    int          built_for_ch_   {0};

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


// Ring modulator.
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

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (!inputs_.empty())
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        const audio_spec spec = dst.buffer_ref().spec();
        assert(spec.format == audio_format::FLOAT);
        const size_t frames = dst.frames();
        float*      out  = reinterpret_cast<float*>(dst.begin());
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

private:
    float carrier_hz_;
    float mix_;
    float phase_ {0.f};
};


// 1-input 1-output pass-through node that captures samples for waveform display.
class visualizer_node : public node
{
public:
    explicit visualizer_node(node_id id) : node(id) {}

    void set_feedback(std::shared_ptr<nb::visualizer_feedback> fb) { fb_ = std::move(fb); }

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (!inputs_.empty())
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        if (fb_)
        {
            const audio_spec spec = dst.buffer_ref().spec();
            assert(spec.format == audio_format::FLOAT);
            fb_->sample_rate = spec.frequency;
            const float* ptr = reinterpret_cast<const float*>(dst.begin());
            fb_->push_samples(ptr, spec.channels, dst.frames());
        }
    }

private:
    std::shared_ptr<nb::visualizer_feedback> fb_;
};


// Chorus: N voices, each a short LFO-modulated delay tap, summed with dry.
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
        built_for_freq_ = 0;
    }

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (!inputs_.empty())
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        const audio_spec spec = dst.buffer_ref().spec();
        assert(spec.format == audio_format::FLOAT);
        const size_t frames = dst.frames();
        ensure_lines(spec);

        float*     out  = reinterpret_cast<float*>(dst.begin());
        const int  ch   = spec.channels;
        const float sr  = static_cast<float>(spec.frequency);
        const float wet = std::min(std::max(mix_, 0.f), 1.f);
        const float dry = 1.f - wet;
        const int   nv  = std::max(1, std::min(static_cast<int>(voices_), 4));
        const float step = 2.f * 3.14159265f * rate_hz_ / sr;
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
                    const float voice_phase = lfo_phase_
                        + static_cast<float>(v) * (2.f * 3.14159265f / static_cast<float>(nv));
                    const float lfo = std::sin(voice_phase);
                    const float delay_smp = base_delay + lfo * max_delay_smp * 0.5f;

                    auto& line = lines_[static_cast<size_t>(v * ch + c)];
                    line.buf[line.write_pos] = dry_s;
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

private:
    float rate_hz_;
    float depth_ms_;
    float voices_;
    float mix_;
    float lfo_phase_ {0.f};

    struct circ_line { std::vector<float> buf; size_t write_pos {0}; };
    std::vector<circ_line> lines_;
    unsigned int built_for_freq_ {0};
    int          built_for_ch_   {0};

    void ensure_lines(audio_spec spec)
    {
        const int nv = std::max(1, std::min(static_cast<int>(voices_), 4));
        if (built_for_freq_ == spec.frequency && built_for_ch_ == spec.channels
            && static_cast<int>(lines_.size()) == nv * spec.channels)
            return;
        const size_t max_smp = static_cast<size_t>(depth_ms_ * 0.001f * static_cast<float>(spec.frequency)) + 4;
        lines_.assign(static_cast<size_t>(nv * spec.channels), circ_line{});
        for (auto& l : lines_) { l.buf.assign(max_smp, 0.f); l.write_pos = 0; }
        built_for_freq_ = spec.frequency;
        built_for_ch_   = spec.channels;
    }
};


// Waveshaper: non-linear distortion via drive + selectable shape curve.
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

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (!inputs_.empty())
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        const audio_spec spec = dst.buffer_ref().spec();
        assert(spec.format == audio_format::FLOAT);
        const size_t frames = dst.frames();
        float*      out  = reinterpret_cast<float*>(dst.begin());
        const size_t cnt = frames * static_cast<size_t>(spec.channels);
        const float wet  = std::min(std::max(mix_,   0.f), 1.f);
        const float dry  = 1.f - wet;
        const float drv  = std::max(1.f, drive_);
        const float norm = 1.f / std::tanh(drv);
        const int   mode = static_cast<int>(std::round(std::min(std::max(shape_, 0.f), 2.f)));

        for (size_t i = 0; i < cnt; ++i)
        {
            const float in_s = out[i];
            float shaped;
            switch (mode)
            {
                case 1:
                {
                    float x = in_s * drv;
                    shaped  = x < -1.f ? -1.f : (x > 1.f ? 1.f : x);
                    break;
                }
                case 2:
                {
                    float x = in_s * drv;
                    while (x >  1.f) x =  2.f - x;
                    while (x < -1.f) x = -2.f - x;
                    shaped = x;
                    break;
                }
                default:
                    shaped = std::tanh(in_s * drv) * norm;
                    break;
            }
            out[i] = dry * in_s + wet * shaped;
        }
    }

private:
    float drive_;
    float shape_;
    float mix_;
};


// Phaser: cascade of N all-pass filters whose centre frequency is swept by an LFO.
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

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (!inputs_.empty())
            inputs_[0]->pull(dst, gen);
        else
            std::fill(dst.begin(), dst.end(), std::byte{0});

        const audio_spec spec = dst.buffer_ref().spec();
        assert(spec.format == audio_format::FLOAT);
        const size_t frames = dst.frames();
        ensure_state(spec);

        float*      out  = reinterpret_cast<float*>(dst.begin());
        const int   ch   = spec.channels;
        const float sr   = static_cast<float>(spec.frequency);
        const float wet  = std::min(std::max(mix_,      0.f), 1.f);
        const float dry  = 1.f - wet;
        const float fb   = std::min(std::max(feedback_, 0.f), 0.99f);
        const float step = 2.f * 3.14159265f * rate_hz_ / sr;
        const int   nst  = num_stages();

        const float base_hz  = 200.f;
        const float top_hz   = sr * 0.45f;
        const float range_hz = (top_hz - base_hz) * std::min(std::max(depth_, 0.f), 1.f);

        for (size_t f = 0; f < frames; ++f)
        {
            const float lfo  = 0.5f * (1.f + std::sin(lfo_phase_));
            const float fc   = base_hz + lfo * range_hz;
            const float tanw = std::tan(3.14159265f * fc / sr);
            const float a    = (tanw - 1.f) / (tanw + 1.f);

            for (int c = 0; c < ch; ++c)
            {
                float x = out[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                x += fb * fb_state_[static_cast<size_t>(c)];

                float y = x;
                for (int s = 0; s < nst; ++s)
                {
                    auto& st = ap_state_[static_cast<size_t>(c * MAX_STAGES + s)];
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
    std::vector<ap_state> ap_state_;
    std::vector<float>    fb_state_;

    int built_for_ch_ {0};

    int num_stages() const
    {
        int n = static_cast<int>(std::round(stages_));
        n = n < 2 ? 2 : (n > MAX_STAGES ? MAX_STAGES : n);
        return (n + 1) & ~1;
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


// Internal fan-out buffer: caches one pull per generation so multiple downstream
// nodes can share a single upstream pull. Automatically injected by the graph
// manager when a graphplan node fans out to more than one consumer.
// On frame-count mismatch between the cached pull and a subsequent pull in the
// same generation, the output is clipped or zero-padded and a warning is logged.
class fan_out_node : public node
{
public:
    explicit fan_out_node(node_id id) : node(id) {}

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        const audio_spec spec   = dst.buffer_ref().spec();
        const size_t     frames = dst.frames();

        if (gen != last_gen_)
        {
            // New generation — pull fresh from the single input.
            ensure_cache(spec, frames);
            std::fill(cache_->data().begin(), cache_->data().end(), std::byte{0});
            if (!inputs_.empty())
            {
                auto cache_span = cache_->as_span();
                inputs_[0]->pull(cache_span, gen);
            }
            last_gen_ = gen;
        }
        else if (cache_ && (cache_->frames() != frames || cache_->spec() != spec))
        {
            log::warn("[fan_out_node %d] frame/spec mismatch: cached=%zu requested=%zu"
                      " — clipping/padding", id(), cache_->frames(), frames);
        }

        // Serve cached data; clip if cache is larger, zero-pad if smaller.
        if (!cache_) { std::fill(dst.begin(), dst.end(), std::byte{0}); return; }
        const size_t stride      = audio_format_size(spec.format) * static_cast<size_t>(spec.channels);
        const size_t copy_frames = std::min(frames, cache_->frames());
        std::memcpy(dst.begin(), cache_->data().data(), copy_frames * stride);
        if (copy_frames < frames)
            std::fill(dst.begin() + copy_frames * stride, dst.end(), std::byte{0});
    }

private:
    uint64_t                      last_gen_ {~uint64_t{0}}; // max → always fresh on first pull
    std::unique_ptr<audio_buffer> cache_;

    void ensure_cache(audio_spec spec, size_t frames)
    {
        if (!cache_ || cache_->spec() != spec || cache_->frames() != frames)
            cache_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }
};


// Pitch shifter via linear-interpolation resampling.
// pitch_ratio > 1.0 = pitch up (consume more source frames per output frame).
// pitch_ratio < 1.0 = pitch down.
class pitch_node : public node
{
public:
    pitch_node(node_id id, float pitch_ratio)
        : node(id), pitch_ratio_(pitch_ratio) {}

    void set_pitch_ratio(float ratio) { pitch_ratio_ = ratio; }
    float pitch_ratio() const { return pitch_ratio_; }

    void pull(audio_buffer::span& dst, uint64_t gen) override
    {
        if (inputs_.empty())
        {
            std::fill(dst.begin(), dst.end(), std::byte{0});
            return;
        }

        const audio_spec spec = dst.buffer_ref().spec();
        assert(spec.format == audio_format::FLOAT);
        const size_t out_frames = dst.frames();
        const int    ch         = spec.channels;

        // How many source frames do we need to produce out_frames at pitch_ratio_?
        const size_t in_frames = static_cast<size_t>(
            std::ceil(static_cast<double>(out_frames) * static_cast<double>(pitch_ratio_))) + 2;

        ensure_scratch(spec, in_frames);
        std::fill(scratch_->data().begin(), scratch_->data().end(), std::byte{0});
        auto scratch_span = scratch_->as_span();
        inputs_[0]->pull(scratch_span, gen);

        const float* src     = reinterpret_cast<const float*>(scratch_->data().data());
        float*       out_buf = reinterpret_cast<float*>(dst.begin());

        for (size_t i = 0; i < out_frames; ++i)
        {
            const double src_pos = static_cast<double>(i) * static_cast<double>(pitch_ratio_);
            const size_t i0      = static_cast<size_t>(src_pos);
            const size_t i1      = (i0 + 1 < in_frames) ? i0 + 1 : i0;
            const float  t       = static_cast<float>(src_pos - static_cast<double>(i0));

            for (int c = 0; c < ch; ++c)
            {
                const float s0 = src[i0 * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                const float s1 = src[i1 * static_cast<size_t>(ch) + static_cast<size_t>(c)];
                out_buf[i * static_cast<size_t>(ch) + static_cast<size_t>(c)] =
                    s0 * (1.f - t) + s1 * t;
            }
        }
    }

private:
    float pitch_ratio_ {1.f};
    std::unique_ptr<audio_buffer> scratch_;

    void ensure_scratch(audio_spec spec, size_t frames)
    {
        if (!scratch_ || scratch_->spec() != spec || scratch_->frames() != frames)
            scratch_ = std::make_unique<audio_buffer>(spec, frames, nullptr);
    }
};


// ---------------------------------------------------------------------------
// lpc_node — built-in TMS5220-compatible LPC speech synthesis
// ---------------------------------------------------------------------------

// Audio graph source node backed by the built-in TMS5220-compatible LPC synthesizer.
// call say() from the game thread to enqueue synthesized speech;
// pull() is called from the audio thread and converts from S16 8kHz mono.
class lpc_node : public node
{
    static constexpr int LPC_RATE = 8000;
    static inline const audio_spec LPC_SPEC { audio_format::S16, 1, LPC_RATE };

public:
    explicit lpc_node(node_id id) : node(id) {}

    // Enqueue a word (TMS5220 bitstream). Call from game thread only.
    // Synthesis runs synchronously here so the audio thread only drains samples.
    void say(const uint8_t* data, size_t length)
    {
        audio_producer_lpc lpc(data, length);
        const size_t total = lpc.total_frames();
        if (total == 0) return;

        audio_buffer tmp(LPC_SPEC, total, nullptr);
        lpc.frames_pull(tmp.as_span(), total);

        const int16_t* src = reinterpret_cast<const int16_t*>(tmp.data().data());

        std::lock_guard<std::mutex> lk(mutex_);
        if (volume_ != 1.f)
        {
            pending_.reserve(pending_.size() + total);
            for (size_t i = 0; i < total; ++i)
                pending_.push_back(static_cast<int16_t>(
                    std::clamp(static_cast<float>(src[i]) * volume_, -32768.f, 32767.f)));
        }
        else
        {
            pending_.insert(pending_.end(), src, src + total);
        }
    }

    // Enqueue silence. Call from game thread only.
    void silence_ms(uint16_t ms)
    {
        const size_t frames = (static_cast<size_t>(ms) * LPC_RATE) / 1000;
        std::lock_guard<std::mutex> lk(mutex_);
        pending_.insert(pending_.end(), frames, int16_t{0});
    }

    // Speak a natural-language phrase against the LPC vocabulary. Game thread only.
    void say_phrase(const char* phrase)
    {
        size_t word_count = 0;
        const nb::lpc_vocab_entry* vocab = nb::lpc_vocab_all(word_count);

        auto find_word = [&](const char* word) -> const nb::lpc_vocab_entry*
        {
            for (size_t i = 0; i < word_count; i++)
            {
                const char* a = word;
                const char* b = vocab[i].word;
                while (*a && *b)
                {
                    if (std::toupper(static_cast<unsigned char>(*a)) !=
                        std::toupper(static_cast<unsigned char>(*b))) break;
                    ++a; ++b;
                }
                if (*a == '\0' && *b == '\0') return &vocab[i];
            }
            return nullptr;
        };

        std::string token;
        for (const char* p = phrase; ; ++p)
        {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (*p == ',' || *p == '.' || *p == '\0' || std::isspace(c))
            {
                if (!token.empty())
                {
                    if (const auto* e = find_word(token.c_str()))
                        say(e->variants[0], e->variant_lengths[0]);
                    token.clear();
                }
                if (*p == ',')      { silence_ms(150); silence_ms(150); }
                else if (*p == '.') { silence_ms(150); silence_ms(150); silence_ms(150); }
                if (*p == '\0') break;
            }
            else
            {
                token += static_cast<char>(c);
            }
        }
    }

    void set_volume(float vol) { volume_ = vol; }

    void pull(audio_buffer::span& dst, uint64_t /*gen*/) override
    {
        const audio_spec graph_spec = dst.buffer_ref().spec();
        ensure_converter(graph_spec);

        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!pending_.empty())
            {
                audio_buffer tmp(LPC_SPEC, pending_.size(),
                    reinterpret_cast<const std::byte*>(pending_.data()));
                auto sp = tmp.as_span();
                converter_->put(sp);
                pending_.clear();
            }
        }

        size_t got = converter_->take(audio_buffer::span(dst));
        if (got < dst.frames())
        {
            const size_t stride = audio_format_size(graph_spec.format)
                                  * static_cast<size_t>(graph_spec.channels);
            std::fill(dst.begin() + got * stride, dst.end(), std::byte{0});
        }
    }

private:
    std::vector<int16_t>                  pending_;
    std::mutex                            mutex_;
    std::unique_ptr<nb::audio_converter>  converter_;
    audio_spec                            converter_out_spec_;
    float                                 volume_ {1.f};

    void ensure_converter(audio_spec out_spec)
    {
        if (!converter_ || converter_out_spec_ != out_spec)
        {
            converter_          = std::make_unique<nb::audio_converter>(LPC_SPEC, out_spec);
            converter_out_spec_ = out_spec;
        }
    }
};

// UI feedback/state for the lpc_node editor widget.
struct lpc_feedback
{
    std::weak_ptr<lpc_node> node_wptr;

    int  ui_word_idx    {0};
    int  ui_variant_idx {0};
    int  ui_silence_ms  {500};
    char ui_phrase[256] {};
};


#ifdef NEWBASE_TALKIE_PCM

// Audio graph source node backed by the TalkiePCM LPC speech synthesizer.
// call say() from the game thread to enqueue synthesized speech;
// pull() is called from the audio thread and converts from S16 8kHz mono.
class talkie_pcm_node : public node
{
public:
    explicit talkie_pcm_node(node_id id) : node(id)
    {
        talkie_.setDataCallback(&talkie_pcm_node::s_callback);
    }

    // Enqueue a word. Call from game thread only.
    void say(const uint8_t* word)
    {
        tl_current = this;
        talkie_.say(word);
        tl_current = nullptr;
    }

    // Enqueue a spoken number (-999999..999999). Call from game thread only.
    void say_number(long number)
    {
        tl_current = this;
        talkie_.sayNumber(number);
        tl_current = nullptr;
    }

    void say_pause()
    {
        tl_current = this;
        talkie_.sayPause();
        tl_current = nullptr;
    }

    // Enqueue silence for the given duration. Call from game thread only.
    void silence_ms(uint16_t ms)
    {
        tl_current = this;
        talkie_.silence(ms);
        tl_current = nullptr;
    }

    // Speak a natural-language phrase. Call from game thread only.
    // Words are matched case-insensitively against the vocabulary; unknown words
    // are spelled out letter by letter. Integers are spoken via say_number().
    // Commas produce a double pause; periods produce a triple pause.
    void say_phrase(const char* phrase)
    {
        size_t vocab_count = 0;
        const nb::talkie_vocab_entry* vocab = nb::talkie_vocab_all(vocab_count);

        auto find_word = [&](const char* word) -> const nb::talkie_vocab_entry*
        {
            for (size_t i = 0; i < vocab_count; i++)
            {
                const char* a = word;
                const char* b = vocab[i].word;
                while (*a && *b)
                {
                    if (std::toupper(static_cast<unsigned char>(*a)) !=
                        std::toupper(static_cast<unsigned char>(*b))) break;
                    ++a; ++b;
                }
                if (*a == '\0' && *b == '\0') return &vocab[i];
            }
            return nullptr;
        };

        auto flush_token = [&](const std::string& token)
        {
            if (token.empty()) return;
            // Try integer
            char* end = nullptr;
            long num = std::strtol(token.c_str(), &end, 10);
            if (end == token.c_str() + token.size())
            {
                say_number(num);
                return;
            }
            // Try vocab lookup
            if (const auto* e = find_word(token.c_str()))
            {
                say(e->variants[0]);
                return;
            }
            // Spell letter by letter
            for (char c : token)
            {
                char letter[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(c))), '\0' };
                if (const auto* e = find_word(letter))
                    say(e->variants[0]);
            }
        };

        std::string token;
        for (const char* p = phrase; ; ++p)
        {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (*p == ',' || *p == '.' || *p == '\0' || std::isspace(c))
            {
                flush_token(token);
                token.clear();
                if (*p == ',')      { say_pause(); say_pause(); }
                else if (*p == '.') { say_pause(); say_pause(); say_pause(); }
                if (*p == '\0') break;
            }
            else
            {
                token += static_cast<char>(c);
            }
        }
    }

    void set_volume(float vol) { talkie_.setVolume(vol); }

    void pull(audio_buffer::span& dst, uint64_t /*gen*/) override
    {
        const audio_spec graph_spec = dst.buffer_ref().spec();
        ensure_converter(graph_spec);

        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!pending_.empty())
            {
                static const audio_spec TALKIE_SPEC { audio_format::S16, 1, FS };
                audio_buffer tmp(TALKIE_SPEC, pending_.size(),
                    reinterpret_cast<const std::byte*>(pending_.data()));
                auto sp = tmp.as_span();
                converter_->put(sp);
                pending_.clear();
            }
        }

        size_t got = converter_->take(audio_buffer::span(dst));
        if (got < dst.frames())
        {
            const size_t stride = audio_format_size(graph_spec.format)
                                  * static_cast<size_t>(graph_spec.channels);
            std::fill(dst.begin() + got * stride, dst.end(), std::byte{0});
        }
    }

private:
    TalkiePCM            talkie_;
    std::vector<int16_t> pending_;
    std::mutex           mutex_;
    std::unique_ptr<nb::audio_converter> converter_;
    audio_spec           converter_out_spec_;

    static inline thread_local talkie_pcm_node* tl_current = nullptr;

    static void s_callback(int16_t* data, int len)
    {
        if (!tl_current) return;
        std::lock_guard<std::mutex> lk(tl_current->mutex_);
        tl_current->pending_.insert(tl_current->pending_.end(), data, data + len);
    }

    void ensure_converter(audio_spec out_spec)
    {
        if (!converter_ || converter_out_spec_ != out_spec)
        {
            static const audio_spec TALKIE_SPEC { audio_format::S16, 1, FS };
            converter_     = std::make_unique<nb::audio_converter>(TALKIE_SPEC, out_spec);
            converter_out_spec_ = out_spec;
        }
    }
};

// UI feedback/state for the talkie_pcm_node editor widget.
// Stored in graphplan::node_data::user_data; accessed only from the main thread.
struct talkie_pcm_feedback
{
    std::weak_ptr<talkie_pcm_node> node_wptr;

    // Widget state
    int  ui_word_idx    {0};
    int  ui_variant_idx {0};
    int  ui_number      {0};
    int  ui_silence_ms  {500};
    char ui_phrase[256] {};
};

#endif // NEWBASE_TALKIE_PCM

} // namespace nb::audio_graph
