#pragma once

#include <newbase/nb_config.h>
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
    PITCH        = 19, // pitch shifter via linear-interpolation resampling
#ifdef NEWBASE_TALKIE_PCM
    TALKIE_PCM_SOURCE = 20, // TalkiePCM LPC speech synthesis source
#endif
    GROUP        = 21, // subgraph node referencing an rgraphplan resource
    GROUP_INPUT  = 22, // audio entry point within a group subgraph
    GROUP_OUTPUT = 23, // audio exit point within a group subgraph
    LPC_SOURCE   = 24, // built-in TMS5220-compatible LPC speech synthesis source
};

class node
{
public:
    explicit node(node_id id) : id_(id) {}
    virtual ~node() = default;

    const node_id& id() const { return id_; }

    // Pull `dst.frames()` frames of audio into dst.
    // Recursively pulls from inputs_ as needed.
    // gen: monotonically increasing generation counter (reserved for fan-out caching).
    virtual void pull(audio_buffer::span& dst, uint64_t gen) = 0;

    // Called by graph::_wire_inputs() after topology changes.
    void set_inputs(std::vector<node*> inputs) { inputs_ = std::move(inputs); }

protected:
    std::vector<node*> inputs_;

private:
    node_id id_;
};

} // namespace nb::audio_graph
