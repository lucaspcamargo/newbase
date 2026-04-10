#pragma once

#include <newbase/audio/types.hpp>
#include <newbase/audio/buffer.hpp>
#include <vector>
#include <memory>

namespace nb::audio_graph
{

using node_id = int;

enum class node_type
{
    OUTPUT       = 0, // final audio sink (always id=0)
    SOURCE       = 1, // wraps an audio_producer (producer assigned externally)
    MIXER        = 2, // N inputs → 1 mixed output
    VORBIS_SOURCE= 3, // source backed by an rvorbis resource
    SINE_SOURCE  = 4, // generates a sine wave (props: frequency, amplitude)
    NOISE_SOURCE = 5, // generates white noise (props: amplitude)
    REVERB       = 6, // Schroeder reverb (props: room_size, damping, wet)
    COMPRESSOR   = 7, // dynamics compressor with auto-duck sidechain
    EQ5          = 8, // 5-band parametric EQ
    GAIN         = 9, // simple gain (props: gain_db)
    BUS_INPUT    = 10, // sends audio into a named bus
    BUS_OUTPUT   = 11, // receives audio from a named bus
    VISUALIZER   = 12, // 1-in 1-out pass-through that captures samples for visualisation
    BITCRUSHER   = 13, // reduces bit depth and/or sample rate
    DELAY        = 14, // delay line with feedback
    RING_MOD     = 15, // ring modulator (multiply by sine carrier)
    CHORUS       = 16, // multi-voice LFO-modulated delay for lush/spatial effects
    WAVESHAPER   = 17, // non-linear distortion via drive + shape curve
    PHASER       = 18, // all-pass cascade swept by an LFO
};

class node
{
public:
    explicit node(node_id id) : id_(id) {}
    virtual ~node() = default;

    const node_id& id() const { return id_; }

    // Called during produce pass.
    // inputs: output buffers of upstream nodes (empty for source nodes).
    virtual void process(audio_spec spec, size_t frames,
                         const std::vector<audio_buffer*>& inputs) = 0;

    // Returns the output buffer populated by the last process() call.
    // May be nullptr before process() is first called.
    virtual audio_buffer* output_buffer() = 0;

private:
    node_id id_;
};

} // namespace nb::audio_graph
