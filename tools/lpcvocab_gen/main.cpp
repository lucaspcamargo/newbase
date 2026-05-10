// lpcvocab_gen — converts TalkiePCM vocabulary headers to the .rlpcvocab binary format
//
// Binary format (little-endian):
//   magic[4] "RLPV" | version uint16 | word_count uint32
//   Per word:  name_len uint16 | name[name_len] | var_count uint8
//   Per variant: vname_len uint8 | vname[vname_len] | data_len uint32 | data[data_len]

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

// Provide dtostrf for non-Arduino platforms so TalkiePCM.h compiles
static inline char* dtostrf(double val, signed char, unsigned char prec, char* buf)
{
    std::snprintf(buf, 14, "%.*f", (int)prec, val);
    return buf;
}

#include <newbase/audio/talkie_pcm_vocab.hpp>

// ---------------------------------------------------------------------------
// TMS5220 LSB-first bitstream scanner — returns byte length up to and
// including the stop frame (energy == 0xF).
// ---------------------------------------------------------------------------
static size_t scan_lpc_length(const uint8_t* data)
{
    size_t  byte_pos = 0;
    int     bit_pos  = 0;
    uint8_t cur_byte = 0;

    auto rev = [](uint8_t b) -> uint8_t {
        b = (b & 0xF0u) >> 4 | (b & 0x0Fu) << 4;
        b = (b & 0xCCu) >> 2 | (b & 0x33u) << 2;
        b = (b & 0xAAu) >> 1 | (b & 0x55u) << 1;
        return b;
    };

    auto get_bits = [&](int n) -> int {
        int val = 0;
        for (int i = 0; i < n; ++i) {
            if (bit_pos == 0)
                cur_byte = rev(data[byte_pos]);
            val |= ((cur_byte >> bit_pos) & 1) << i;
            if (++bit_pos == 8) { bit_pos = 0; ++byte_pos; }
        }
        return val;
    };

    for (;;) {
        int energy_idx = get_bits(4);
        if (energy_idx == 0xF) break;  // stop frame — done
        if (energy_idx == 0x0) continue; // silent frame — just 4 bits

        bool repeat    = static_cast<bool>(get_bits(1));
        int period_idx = get_bits(6);

        if (!repeat) {
            get_bits(5 + 5 + 4 + 4); // K1–K4
            if (period_idx != 0)
                get_bits(4 + 4 + 4 + 3 + 3 + 3); // K5–K10
        }
    }

    // Include the partial byte that holds the stop frame bits
    return bit_pos > 0 ? byte_pos + 1 : byte_pos;
}

// ---------------------------------------------------------------------------
// Little-endian write helpers
// ---------------------------------------------------------------------------
static void write_u8(FILE* f, uint8_t v)   { fwrite(&v, 1, 1, f); }
static void write_u16(FILE* f, uint16_t v)
{
    uint8_t b[2] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8) };
    fwrite(b, 1, 2, f);
}
static void write_u32(FILE* f, uint32_t v)
{
    uint8_t b[4] = {
        static_cast<uint8_t>(v),       static_cast<uint8_t>(v >> 8),
        static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)
    };
    fwrite(b, 1, 4, f);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: lpcvocab_gen <output.rlpcvocab>\n");
        return 1;
    }

    size_t word_count = 0;
    const nb::talkie_vocab_entry* vocab = nb::talkie_vocab_all(word_count);

    FILE* out = fopen(argv[1], "wb");
    if (!out) {
        fprintf(stderr, "error: could not open '%s' for writing\n", argv[1]);
        return 1;
    }

    // Header
    fwrite("RLPV", 1, 4, out);
    write_u16(out, 1); // version
    write_u32(out, static_cast<uint32_t>(word_count));

    size_t total_bytes = 0;
    for (size_t w = 0; w < word_count; ++w)
    {
        const nb::talkie_vocab_entry& entry = vocab[w];
        const char* word_name = entry.word ? entry.word : "";
        const uint16_t name_len = static_cast<uint16_t>(strlen(word_name));

        write_u16(out, name_len);
        fwrite(word_name, 1, name_len, out);
        write_u8(out, static_cast<uint8_t>(entry.variant_count));

        for (size_t v = 0; v < entry.variant_count; ++v)
        {
            const char* vname = entry.variant_names[v] ? entry.variant_names[v] : "";
            const uint8_t vname_len = static_cast<uint8_t>(strlen(vname));

            const uint8_t* data = entry.variants[v];
            const size_t   data_len = scan_lpc_length(data);

            write_u8(out, vname_len);
            fwrite(vname, 1, vname_len, out);
            write_u32(out, static_cast<uint32_t>(data_len));
            fwrite(data, 1, data_len, out);
            total_bytes += data_len;
        }
    }

    fclose(out);
    fprintf(stdout, "wrote %zu words, %zu bytes of LPC data → %s\n",
            word_count, total_bytes, argv[1]);
    return 0;
}
