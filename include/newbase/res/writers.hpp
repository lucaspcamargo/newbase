#pragma once

#include <newbase/res/resource.hpp>
#include <entt/core/ident.hpp>

namespace nb {

// Writer functions: serialize an in-memory resource back to storage.
// Each function encodes the resource's current state and calls rman().write_all_sync().

bool rwriter_texture(nb::resource* res);

} // namespace nb
