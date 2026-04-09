#pragma once

#include <newbase/audio/producer.hpp>
#include <cmath>
#include <cstdlib>

namespace nb {

// Generates a continuous sine wave in FLOAT format.
// frequency_hz: oscillator frequency in Hz.
// amplitude:    peak amplitude [0..1].
class audio_producer_sine : public audio_producer
{
public:
    audio_producer_sine(audio_spec spec, float frequency_hz, float amplitude)
        : spec_(spec), frequency_hz_(frequency_hz), amplitude_(amplitude), phase_(0.f)
    {
        spec_.format = audio_format::FLOAT;
    }

    audio_spec spec() override { return spec_; }

    size_t frames_pull(audio_buffer::span dst, size_t max_frames) override
    {
        float* out     = reinterpret_cast<float*>(dst.begin());
        const float dt = 1.f / static_cast<float>(spec_.frequency);
        const float w  = 2.f * 3.14159265f * frequency_hz_;

        for (size_t i = 0; i < max_frames; ++i)
        {
            float v = amplitude_ * std::sin(phase_);
            phase_ += w * dt;
            if (phase_ > 2.f * 3.14159265f)
                phase_ -= 2.f * 3.14159265f;
            for (int ch = 0; ch < spec_.channels; ++ch)
                out[i * spec_.channels + ch] = v;
        }
        return max_frames;
    }

    void set_frequency(float hz)        { frequency_hz_ = hz; }
    void set_amplitude(float amplitude) { amplitude_    = amplitude; }

private:
    audio_spec spec_;
    float      frequency_hz_;
    float      amplitude_;
    float      phase_;
};


// Generates white noise in FLOAT format.
// amplitude: peak amplitude [0..1].
class audio_producer_noise : public audio_producer
{
public:
    audio_producer_noise(audio_spec spec, float amplitude)
        : spec_(spec), amplitude_(amplitude)
    {
        spec_.format = audio_format::FLOAT;
    }

    audio_spec spec() override { return spec_; }

    size_t frames_pull(audio_buffer::span dst, size_t max_frames) override
    {
        float* out = reinterpret_cast<float*>(dst.begin());
        for (size_t i = 0; i < max_frames; ++i)
        {
            float v = amplitude_ * (2.f * (static_cast<float>(std::rand()) /
                                           static_cast<float>(RAND_MAX)) - 1.f);
            for (int ch = 0; ch < spec_.channels; ++ch)
                out[i * spec_.channels + ch] = v;
        }
        return max_frames;
    }

    void set_amplitude(float amplitude) { amplitude_ = amplitude; }

private:
    audio_spec spec_;
    float      amplitude_;
};

} // namespace nb
