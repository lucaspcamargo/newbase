#pragma once
#include <string>
#include <cstring>
#include <entt/entt.hpp>

namespace nb {

struct cstructure {
    static constexpr int NAME_CAP = 32;

    char         name[NAME_CAP] {};         // zero-init = anonymous
    entt::entity parent       { entt::null };
    entt::entity first_child  { entt::null };
    entt::entity next_sibling { entt::null };

    bool        has_name()   const { return name[0] != '\0'; }
    std::string get_name()   const { return std::string(name); }
    void set_name(std::string s) {
        auto n = std::min(s.size(), size_t(NAME_CAP - 1));
        std::memcpy(name, s.data(), n);
        name[n] = '\0';
    }

    static void _ensure_rtti();
};

} // namespace nb
