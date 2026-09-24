#pragma once

// My sketch of sequencer types for the audio system
// In contrast with most of the audio system, that works with PCM buffers
// and streams, these classes deal in sequencer events.
// You can think of an event as a MIDI note playing, or an automation command,
// or something else.

// The most important part is that sequencer events are ordered and timestamped.
// They occur at a specific moment in time during the stream, and are driven by
// a clock. Unsually there will be a clock reference to obtain events, or a
// batch of events, from a stream.

// Anyway, nothing revolutionary. I just want to be able to use sequenced music
// with synths and emulated chips. Standardizing on an interface for that opens
// up interesting possibilities, for the audio graph and game content.

// NOTE that this API here is just a first draft. Certainly, it will be better
//      defined as real implementation begins.


#include <cmath>
#include <cstdint>
#include <string_view>
#include <cstring>  // for strnlen and strncpy


namespace nb::audio::seq
{
    /// an audio frame index, used in sequecer types
    using frame_t = uint64_t;


    /// Enum for event types described by the API.
    enum class event_type : uint8_t
    {
        // Sentinel value.
        INVALID,

        /// For now, there's only MIDI.
        /// There could be others, especially for VGM-type stuff.
        MIDI,

        /// For looping, denotes a state we may want to return to
        LOOP_STATE_SAVE,

        /// For looping, requests restore to previous saved state
        LOOP_STATE_RESTORE,

        /// General-purpose marker
        MARKER,

        /// count of all possible event types
        EVENT_TYPE_COUNT
    };


    /// A stream event
    /// It is currently defined as an 8-byte timestamp, a 7-bytes union
    /// of event data, and 1 byte for the event type.
    struct event
    {
        /// The event offset, or timestamp, on the stream
        frame_t offset {0};

        union
        {
            /// MIDI data, if event is of type event_type::MIDI
            struct {
                /// The MIDI status byte.
                /// When a Channel Voice message, it goes from 0x80 to 0xEF
                uint8_t status;

                /// The MIDI data1 byte
                uint8_t data1;

                /// The MIDI data2 byte
                uint8_t data2;

                /// helper for extracting the command out of a status event
                constexpr uint8_t command() const { return status >> 4; }

                /// helper for extracting the channel out of a status event
                constexpr uint8_t channel() const { return status & 0x0F; }
            } midi;

            struct {
                // NOTE up to 6 chars, may not be null-terminated
                //      use marker_name() to get a string view
                private:
                    friend struct event;
                    char data[7];
            } marker;

            uint8_t raw[7];
        };

        /// The type for this event. Last byte of struct, for memory alignment.
        event_type type {event_type::INVALID};

        /// Helper for safely obtaining a string view of a marker id
        constexpr std::string_view marker_id() const {
            if (type != event_type::MARKER) return "";
            return std::string_view(marker.data, strnlen(marker.data, sizeof(marker)));
        }

        /// Helper for safely obtaining a string view of a marker id
        constexpr void marker_set_id(const char *new_id) {
            strncpy(marker.data, new_id, 6);
        }
    };

    static_assert(sizeof(event) == 16, "event struct should be exactly 16 bytes");

    enum class midi_status : uint8_t
    {
        CH_VOICE_MIN = 0x80,
        CH_VOICE_MAX = 0xEF,

        // the ones below are not expected to be used by synths
        // but could be useful for controlling sequencer nodes
        // with other sequencer nodes (?)

        START_SEQUENCE    = 0xFA,
        CONTINUE_SEQUENCE = 0xFB,
        STOP_SEQUENCE     = 0xFC
    };

    // The high nibble of channel voice messages (0x80-0xEF)
    enum class midi_command : uint8_t
    {
        /// turns off a note in the channel voice
        /// data1 is the note
        /// data2 is the release velocity
        NOTE_OFF = 0x8,

        /// turns on a note in the channel voice
        /// data1 is the note
        /// data2 is the attack velocity
        /// attack velocity = 0 must be treated as NOTE_OFF
        NOTE_ON  = 0x9,

        /// controls pressure for individual notes
        /// data1 is the note
        /// data2 is the pressure value
        POLY_KEY_PRESSURE  = 0xA,

        /// changes controller values for the channel
        /// data1 is the controller ID
        /// data2 is the controller value
        /// see the midi_cc_id enum for possible
        CONTROL_CHANGE = 0xB,

        /// changes program/preset for channel (instrument, usually)
        /// data1 is the program/preset ID
        /// data2 is empty (real midi message is 2 bytes)
        PROGRAM_CHANGE = 0xC,

        /// changes overall channel pressure
        /// data1 is the pressure value
        /// data2 is empty (real midi message is 2 bytes)
        CHANNEL_PRESSURE = 0xD,

        /// pitch bend wheel control
        /// data1 is the low 7 bits (fine)
        /// data2 is the high 7 bits (coarse)
        PITCH_BEND = 0xE,
    };

    enum class midi_cc_id : uint8_t
    {
        MODULATION_WHEEL = 1,
        CHANNEL_VOLUME   = 7,
        PAN              = 10, // value: (0 = Left, 64 = Center, 127 = Right)
        EXPRESSION       = 11,
        SUSTAIN_PEDAL    = 64, // value: (< 64 = Off, >= 64 = On)
        ALL_SOUND_OFF   = 120,
        ALL_NOTES_OFF   = 123,
        RESET_ALL_CONTROLLERS = 121
    };

    constexpr float midi_freq(uint8_t midi_note)
    {
        return 440.f * powf(2.f, (midi_note-69)/12.f);
    }
}
