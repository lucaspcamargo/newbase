#pragma once

#include <newbase/graphplan/types.hpp>

namespace nb::graphplan {

// Register a domain so it can be found by name (e.g. by the graphplan resource loader).
// The domain reference must remain valid for the lifetime of the registry entry.
void register_domain(const domain& dom);

// Look up a previously registered domain by its id string. Returns nullptr if not found.
const domain* find_domain(const char* id);

} // namespace nb::graphplan
