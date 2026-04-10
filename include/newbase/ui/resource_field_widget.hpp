#pragma once

#include <newbase/res/resource.hpp>
#include <entt/entt.hpp>
#include <memory>

namespace nb {

// Drag-and-drop payload type used between the resource browser and resource field widgets.
struct res_drag_payload
{
    entt::id_type asset_id      {0};
    entt::id_type res_type_id   {0}; // meta type id of the resource (e.g. "rtexture"_hs.value())
};

inline constexpr const char* RES_DRAG_PAYLOAD_TYPE = "NB_RESOURCE";

// Displays a resource reference field with:
//   - an icon + name + hex id in a read-only text box
//   - a drop target accepting NB_RESOURCE drags (type-checked)
//   - an inline × button to clear the reference
//
// ptr is updated in-place when the user drops or clears.
// Returns true if ptr was modified.
bool draw_resource_field(const char* label, entt::id_type res_type_id,
                         std::shared_ptr<nb::resource>& ptr);

// Variant that stores only the asset id (no resource load needed).
// id == 0 means no resource selected.
// Returns true if id was modified.
bool draw_resource_id_field(const char* label, entt::id_type res_type_id,
                            entt::id_type& id);

} // namespace nb
