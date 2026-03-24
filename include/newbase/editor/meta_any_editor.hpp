#pragma once

#include <entt/entt.hpp>

namespace nb {

// Draws an ImGui editor widget for any entt::meta_any value.
// Returns true if the value was modified.
bool draw_meta_any_editor(const char* label, entt::meta_any& ref, bool recursing = false);

} // namespace nb
