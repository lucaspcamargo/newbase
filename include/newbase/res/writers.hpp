#pragma once

#include <newbase/res/resource.hpp>
#include <entt/core/ident.hpp>

namespace nb {
namespace graphplan { class plan; }

// Writer functions: serialize an in-memory resource back to storage.
// Each function encodes the resource's current state and calls rman().write_all_sync().

bool rwriter_texture(nb::resource* res);

// Serialize a live graphplan to a file (path, not resource id).
// Handles encoding from the plan domain id down to nodes, links, and properties.
bool write_graphplan_plan(const graphplan::plan& plan, const char* path);

} // namespace nb
