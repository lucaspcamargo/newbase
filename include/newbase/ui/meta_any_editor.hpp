#pragma once

#include <entt/entt.hpp>
#include <newbase/reflection/data.hpp>

namespace nb {

// Draws an ImGui editor widget for any entt::meta_any value.
// Returns true if the value was modified.
// hint: optional data_info from the parent struct's field registration, used to select
//       specialised widgets (e.g. DATA_SUBTYPE_COLOR renders a ColorEdit4 for glm::vec4).
bool draw_meta_any_editor(const char* label, entt::meta_any& ref,
                          bool recursing = false,
                          const rtti::data_info* hint = nullptr);

} // namespace nb
