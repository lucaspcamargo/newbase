#include <newbase/audio/producer/lpc.hpp>
#include <newbase/audio/buffer.hpp>
#include <newbase/log.hpp>
#include <cstring>
#include <algorithm>
#include <cassert>

using namespace nb;

// ---- Constants ----------------------------------------------------------------

static constexpr int FS                = 8000;
static constexpr int FRAME_RATE        = 40;
static constexpr int SAMPLES_PER_FRAME = FS / FRAME_RATE; // 200
static constexpr int CHIRP_SIZE        = 41;

static constexpr audio_spec LPC_SPEC { audio_format::S16, 1, FS };

// ---- TMS5220 ROM lookup tables (TI TMS5220 datasheet) -------------------------
// These are the hardware quantization tables embedded in the TMS5220 chip.

static constexpr uint8_t tms_energy[16] = {
    0x00, 0x02, 0x03, 0x04, 0x05, 0x07, 0x0a, 0x0f,
    0x14, 0x20, 0x29, 0x39, 0x51, 0x72, 0xa1, 0xff
};

static constexpr uint8_t tms_period[64] = {
    0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
    0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2d, 0x2f, 0x31,
    0x33, 0x35, 0x36, 0x39, 0x3b, 0x3d, 0x3f, 0x42,
    0x45, 0x47, 0x49, 0x4d, 0x4f, 0x51, 0x55, 0x57,
    0x5c, 0x5f, 0x63, 0x66, 0x6a, 0x6e, 0x73, 0x77,
    0x7b, 0x80, 0x85, 0x8a, 0x8f, 0x95, 0x9a, 0xa0
};

// K1 and K2 are 16-bit fixed-point (shift by 15 in filter)
static constexpr int16_t tms_k1[32] = {
    (int16_t)0x82c0, (int16_t)0x8380, (int16_t)0x83c0, (int16_t)0x8440,
    (int16_t)0x84c0, (int16_t)0x8540, (int16_t)0x8600, (int16_t)0x8780,
    (int16_t)0x8880, (int16_t)0x8980, (int16_t)0x8ac0, (int16_t)0x8c00,
    (int16_t)0x8d40, (int16_t)0x8f00, (int16_t)0x90c0, (int16_t)0x92c0,
    (int16_t)0x9900, (int16_t)0xa140, (int16_t)0xab80, (int16_t)0xb840,
    (int16_t)0xc740, (int16_t)0xd8c0, (int16_t)0xebc0, (int16_t)0x0000,
    (int16_t)0x1440, (int16_t)0x2740, (int16_t)0x38c0, (int16_t)0x47c0,
    (int16_t)0x5480, (int16_t)0x5ec0, (int16_t)0x6700, (int16_t)0x6d40
};

static constexpr int16_t tms_k2[32] = {
    (int16_t)0xae00, (int16_t)0xb480, (int16_t)0xbb80, (int16_t)0xc340,
    (int16_t)0xcb80, (int16_t)0xd440, (int16_t)0xddc0, (int16_t)0xe780,
    (int16_t)0xf180, (int16_t)0xfbc0, (int16_t)0x0600, (int16_t)0x1040,
    (int16_t)0x1a40, (int16_t)0x2400, (int16_t)0x2d40, (int16_t)0x3600,
    (int16_t)0x3e40, (int16_t)0x45c0, (int16_t)0x4cc0, (int16_t)0x5300,
    (int16_t)0x5880, (int16_t)0x5dc0, (int16_t)0x6240, (int16_t)0x6640,
    (int16_t)0x69c0, (int16_t)0x6cc0, (int16_t)0x6f80, (int16_t)0x71c0,
    (int16_t)0x73c0, (int16_t)0x7580, (int16_t)0x7700, (int16_t)0x7e80
};

// K3–K10 are 8-bit fixed-point (shift by 7 in filter)
static constexpr int8_t tms_k3[16] = {
    (int8_t)0x92, (int8_t)0x9f, (int8_t)0xad, (int8_t)0xba,
    (int8_t)0xc8, (int8_t)0xd5, (int8_t)0xe3, (int8_t)0xf0,
    (int8_t)0xfe, 0x0b, 0x19, 0x26, 0x34, 0x41, 0x4f, 0x5c
};

static constexpr int8_t tms_k4[16] = {
    (int8_t)0xae, (int8_t)0xbc, (int8_t)0xca, (int8_t)0xd8,
    (int8_t)0xe6, (int8_t)0xf4, 0x01, 0x0f, 0x1d, 0x2b,
    0x39, 0x47, 0x55, 0x63, 0x71, 0x7e
};

static constexpr int8_t tms_k5[16] = {
    (int8_t)0xae, (int8_t)0xba, (int8_t)0xc5, (int8_t)0xd1,
    (int8_t)0xdd, (int8_t)0xe8, (int8_t)0xf4, (int8_t)0xff,
    0x0b, 0x17, 0x22, 0x2e, 0x39, 0x45, 0x51, 0x5c
};

static constexpr int8_t tms_k6[16] = {
    (int8_t)0xc0, (int8_t)0xcb, (int8_t)0xd6, (int8_t)0xe1,
    (int8_t)0xec, (int8_t)0xf7, 0x03, 0x0e, 0x19, 0x24,
    0x2f, 0x3a, 0x45, 0x50, 0x5b, 0x66
};

static constexpr int8_t tms_k7[16] = {
    (int8_t)0xb3, (int8_t)0xbf, (int8_t)0xcb, (int8_t)0xd7,
    (int8_t)0xe3, (int8_t)0xef, (int8_t)0xfb, 0x07, 0x13,
    0x1f, 0x2b, 0x37, 0x43, 0x4f, 0x5a, 0x66
};

static constexpr int8_t tms_k8[8] = {
    (int8_t)0xc0, (int8_t)0xd8, (int8_t)0xf0, 0x07, 0x1f, 0x37, 0x4f, 0x66
};

static constexpr int8_t tms_k9[8] = {
    (int8_t)0xc0, (int8_t)0xd4, (int8_t)0xe8, (int8_t)0xfc,
    0x10, 0x25, 0x39, 0x4d
};

static constexpr int8_t tms_k10[8] = {
    (int8_t)0xcd, (int8_t)0xdf, (int8_t)0xf1, 0x04, 0x16, 0x28, 0x3b, 0x4d
};

// Voiced excitation waveform (TI TMS5220 application note)
static constexpr uint8_t tms_chirp[CHIRP_SIZE] = {
    0x00, 0x2a, 0xd4, 0x32, 0xb2, 0x12, 0x25, 0x14,
    0x02, 0xe1, 0xc5, 0x02, 0x5f, 0x5a, 0x05, 0x0f,
    0x26, 0xfc, 0xa5, 0xa5, 0xd6, 0xdd, 0xdc, 0xfc,
    0x25, 0x2b, 0x22, 0x21, 0x0f, 0xff, 0xf8, 0xee,
    0xed, 0xef, 0xf7, 0xf6, 0xfa, 0x00, 0x03, 0x02, 0x01
};

// ---- Private state ------------------------------------------------------------

struct nb::audio_producer_lpc_p
{
    const uint8_t* data   {nullptr};
    size_t         length {0};

    // Bitstream reader
    size_t  byte_pos {0};
    uint8_t bit_pos  {0};

    // Current frame parameters
    uint16_t energy {0};
    uint8_t  period {0};
    int16_t  k1 {0}, k2 {0};
    int8_t   k3 {0}, k4 {0}, k5 {0}, k6 {0};
    int8_t   k7 {0}, k8 {0}, k9 {0}, k10{0};

    // Filter delay line
    int16_t x[10] {};

    // Excitation state
    uint8_t  period_counter {0};
    uint16_t rand_state     {1}; // LFSR — must not be zero

    // Pipeline delay (output is 1 sample behind computation, matching hardware)
    int16_t next_sample     {0};
    int     samples_pending {0};
    bool    done            {false};

    // Position tracking
    size_t total_samples    {0};
    size_t consumed_samples {0};

    // ---- Bitstream helpers -----------------------------------------------

    // Reverse bit order within a byte.
    // TMS5220 serial ROM format is LSB-first per byte; reversing before extraction
    // lets us shift bits out from the MSB side as a normal shift register.
    static uint8_t rev(uint8_t a)
    {
        a = (a >> 4) | (a << 4);
        a = ((a & 0xcc) >> 2) | ((a & 0x33) << 2);
        a = ((a & 0xaa) >> 1) | ((a & 0x55) << 1);
        return a;
    }

    uint8_t get_bits(int n)
    {
        if (byte_pos >= length) return 0;
        uint16_t word = (uint16_t)rev(data[byte_pos]) << 8;
        if (bit_pos + n > 8 && byte_pos + 1 < length)
            word |= rev(data[byte_pos + 1]);
        word <<= bit_pos;
        uint8_t val = (uint8_t)(word >> (16 - n));
        bit_pos += (uint8_t)n;
        if (bit_pos >= 8) { bit_pos -= 8; byte_pos++; }
        return val;
    }

    // ---- Frame parsing --------------------------------------------------

    // Returns false on stop frame (energy == 15).
    bool parse_frame()
    {
        uint8_t e = get_bits(4);
        if (e == 0) {
            energy = 0; // silence — keep K values, clear energy
            return true;
        }
        if (e == 15) {
            energy = 0;
            k1 = k2 = 0; k3 = k4 = k5 = k6 = k7 = k8 = k9 = k10 = 0;
            done = true;
            return false;
        }
        energy = tms_energy[e];
        bool repeat = get_bits(1) != 0;
        period = tms_period[get_bits(6)];
        if (!repeat) {
            k1 = tms_k1[get_bits(5)];
            k2 = tms_k2[get_bits(5)];
            k3 = tms_k3[get_bits(4)];
            k4 = tms_k4[get_bits(4)];
            if (period) { // voiced frames carry the full 10 K values
                k5  = tms_k5 [get_bits(4)];
                k6  = tms_k6 [get_bits(4)];
                k7  = tms_k7 [get_bits(4)];
                k8  = tms_k8 [get_bits(3)];
                k9  = tms_k9 [get_bits(3)];
                k10 = tms_k10[get_bits(3)];
            }
        }
        return true;
    }

    // ---- Per-sample synthesis -------------------------------------------

    // Returns the output sample for this tick.
    // One sample behind computation (hardware pipeline delay).
    int16_t synthesize_sample()
    {
        int16_t out = next_sample; // output previous result

        int16_t u[11];

        if (period) {
            // Voiced: periodic chirp modulated by energy
            if (period_counter < period) period_counter++;
            else                         period_counter = 0;
            u[10] = (period_counter < CHIRP_SIZE)
                ? (int16_t)(((uint32_t)tms_chirp[period_counter] * energy) >> 8)
                : 0;
        } else {
            // Unvoiced: maximal-length LFSR (polynomial 0xB800, matches TMS5220)
            rand_state = (rand_state >> 1) ^ ((rand_state & 1) ? 0xB800u : 0u);
            u[10] = (rand_state & 1) ? (int16_t)energy : -(int16_t)energy;
        }

        // Lattice filter — forward path (K10 → K1)
        u[9] = u[10] - (int16_t)(((int16_t)k10 * x[9]) >> 7);
        u[8] = u[9]  - (int16_t)(((int16_t)k9  * x[8]) >> 7);
        u[7] = u[8]  - (int16_t)(((int16_t)k8  * x[7]) >> 7);
        u[6] = u[7]  - (int16_t)(((int16_t)k7  * x[6]) >> 7);
        u[5] = u[6]  - (int16_t)(((int16_t)k6  * x[5]) >> 7);
        u[4] = u[5]  - (int16_t)(((int16_t)k5  * x[4]) >> 7);
        u[3] = u[4]  - (int16_t)(((int16_t)k4  * x[3]) >> 7);
        u[2] = u[3]  - (int16_t)(((int16_t)k3  * x[2]) >> 7);
        u[1] = u[2]  - (int16_t)(((int32_t)k2  * x[1]) >> 15);
        u[0] = u[1]  - (int16_t)(((int32_t)k1  * x[0]) >> 15);

        // Clamp to 10-bit signed range before updating the delay line
        if (u[0] >  511) u[0] =  511;
        if (u[0] < -512) u[0] = -512;

        // Lattice filter — reverse path (updates delay line, K9 → K1)
        x[9] = x[8] + (int16_t)(((int16_t)k9  * u[8]) >> 7);
        x[8] = x[7] + (int16_t)(((int16_t)k8  * u[7]) >> 7);
        x[7] = x[6] + (int16_t)(((int16_t)k7  * u[6]) >> 7);
        x[6] = x[5] + (int16_t)(((int16_t)k6  * u[5]) >> 7);
        x[5] = x[4] + (int16_t)(((int16_t)k5  * u[4]) >> 7);
        x[4] = x[3] + (int16_t)(((int16_t)k4  * u[3]) >> 7);
        x[3] = x[2] + (int16_t)(((int16_t)k3  * u[2]) >> 7);
        x[2] = x[1] + (int16_t)(((int32_t)k2  * u[1]) >> 15);
        x[1] = x[0] + (int16_t)(((int32_t)k1  * u[0]) >> 15);
        x[0] = u[0];

        next_sample = u[0];
        return out;
    }

    // ---- Pre-scan -------------------------------------------------------

    // Parses the bitstream without synthesizing to count total output samples.
    size_t count_total_samples() const
    {
        size_t  bpos  = 0;
        uint8_t bbit  = 0;
        size_t  total = 0;

        auto lrev = [](uint8_t a) -> uint8_t {
            a = (a >> 4) | (a << 4);
            a = ((a & 0xcc) >> 2) | ((a & 0x33) << 2);
            a = ((a & 0xaa) >> 1) | ((a & 0x55) << 1);
            return a;
        };
        auto lget = [&](int n) -> uint8_t {
            if (bpos >= length) return 0;
            uint16_t word = (uint16_t)lrev(data[bpos]) << 8;
            if (bbit + n > 8 && bpos + 1 < length)
                word |= lrev(data[bpos + 1]);
            word <<= bbit;
            uint8_t val = (uint8_t)(word >> (16 - n));
            bbit += (uint8_t)n;
            if (bbit >= 8) { bbit -= 8; bpos++; }
            return val;
        };

        while (bpos < length) {
            uint8_t e = lget(4);
            if (e == 15) break;
            if (e == 0)  { total += SAMPLES_PER_FRAME; continue; }
            bool    repeat = lget(1) != 0;
            uint8_t pidx   = lget(6);
            if (!repeat) {
                lget(5); lget(5);           // K1, K2
                lget(4); lget(4);           // K3, K4
                if (tms_period[pidx]) {     // voiced: K5–K10
                    lget(4); lget(4); lget(4);
                    lget(3); lget(3); lget(3);
                }
            }
            total += SAMPLES_PER_FRAME;
        }
        return total;
    }

    void reset_state()
    {
        byte_pos = 0; bit_pos = 0;
        energy = 0; period = 0;
        k1 = k2 = 0; k3 = k4 = k5 = k6 = k7 = k8 = k9 = k10 = 0;
        memset(x, 0, sizeof(x));
        period_counter = 0; rand_state = 1;
        next_sample = 0; samples_pending = 0;
        done = false; consumed_samples = 0;
    }
};

// ---- Public interface ---------------------------------------------------------

audio_producer_lpc::audio_producer_lpc(const uint8_t* data, size_t length)
{
    _d = new audio_producer_lpc_p();
    _d->data   = data;
    _d->length = length;
    _d->total_samples = _d->count_total_samples();
    log::info("[audio] lpc: %zu bytes -> %zu samples (%.2f s)",
        length, _d->total_samples, (float)_d->total_samples / FS);
}

audio_producer_lpc::~audio_producer_lpc() { delete _d; }

audio_spec audio_producer_lpc::spec()         { return LPC_SPEC; }
bool       audio_producer_lpc::reset()        { _d->reset_state(); return true; }
size_t     audio_producer_lpc::frames_left()  { return _d->total_samples - _d->consumed_samples; }
size_t     audio_producer_lpc::curr_frame()   const { return _d->consumed_samples; }
size_t     audio_producer_lpc::total_frames() const { return _d->total_samples; }

size_t audio_producer_lpc::frames_pull(audio_buffer::span dst, size_t max_frames)
{
    assert(dst.buffer_ref().spec() == LPC_SPEC);

    size_t to_pull = std::min(max_frames, frames_left());
    size_t pulled  = 0;
    int16_t* out   = reinterpret_cast<int16_t*>(dst.begin());

    while (pulled < to_pull)
    {
        if (_d->samples_pending == 0)
        {
            if (_d->done || !_d->parse_frame()) break;
            _d->samples_pending = SAMPLES_PER_FRAME;
        }

        size_t batch = std::min((size_t)_d->samples_pending, to_pull - pulled);
        for (size_t i = 0; i < batch; i++)
        {
            // Scale 10-bit signed [-512,511] to 16-bit [-32768,32704]
            *out++ = (int16_t)(_d->synthesize_sample() << 6);
        }
        _d->samples_pending  -= (int)batch;
        pulled               += batch;
        _d->consumed_samples += batch;
    }

    return pulled;
}
