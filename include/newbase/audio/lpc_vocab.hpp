#pragma once

#include <cstdint>
#include <cstddef>

namespace nb {

struct lpc_vocab_entry {
    const char*           word;
    const uint8_t* const* variants;       // TI serial ROM bitstream data pointers
    const size_t*         variant_lengths; // byte lengths, parallel to variants
    const char* const*    variant_names;
    size_t                variant_count;
};

// Returns the built-in LPC vocabulary. Initially empty; add entries by populating
// the static table in lpc_vocab.hpp with TMS5220-compatible bitstream data.
inline const lpc_vocab_entry* lpc_vocab_all(size_t& out_count)
{
    static const lpc_vocab_entry entries[] = {};
    out_count = 0;
    return entries;
}

} // namespace nb
