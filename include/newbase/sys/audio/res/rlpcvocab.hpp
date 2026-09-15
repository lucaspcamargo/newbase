#pragma once

#include <newbase/res/resource.hpp>
#include <entt/core/hashed_string.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace nb {

// Binary format: .rlpcvocab
// magic[4] "RLPV" | version uint16 | word_count uint32
// Per word:  name_len uint16 | name[name_len] | var_count uint8
// Per variant: vname_len uint8 | vname[vname_len] | data_len uint32 | data[data_len]
// All multi-byte integers: little-endian.

struct rlpcvocab_variant {
    std::string          name;
    std::vector<uint8_t> data; // TMS5220-compatible LPC bitstream
};

struct rlpcvocab_word {
    std::string                      name;
    std::vector<rlpcvocab_variant>   variants;
};

struct rlpcvocab : public resource {
    explicit rlpcvocab(entt::id_type id = 0)
        : resource(id, entt::hashed_string{"rlpcvocab"}.value()) {}

    bool                          valid {false};
    std::vector<rlpcvocab_word>   words;
};

} // namespace nb
