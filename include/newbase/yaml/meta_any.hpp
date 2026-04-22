#pragma once

#include <entt/meta/meta.hpp>
#include <ryml.hpp>

namespace nb {

// Write a meta_any value as a YAML key-value pair into a map node.
// Handles: float, int, bool, std::string, entt::id_type (uint32_t).
// Key and string values are copied into the tree's arena.
void prop_to_yaml(ryml::NodeRef map, c4::csubstr key, const entt::meta_any& val);

// Read a YAML scalar node into a meta_any, using hint's type to guide parsing.
// If hint is empty or the type is unrecognised, stores the value as std::string.
bool prop_from_yaml(ryml::ConstNodeRef scalar, entt::meta_any& out, const entt::meta_any& hint);

} // namespace nb
