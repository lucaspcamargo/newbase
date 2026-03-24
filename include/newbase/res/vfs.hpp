#pragma once

#include <newbase/res/storage/handle.hpp>
#include <entt/entity/registry.hpp>
#include <string>
#include <vector>

namespace nb {

/**
 * @brief VFS tree node component.
 * Attached to every entity in a vfs_tree registry.
 * Leaf nodes (actual assets) additionally have a res_storage::asset_handle component.
 */
struct vfs_node {
    std::string name;                   // path segment, e.g. "player" or "idle.png"
    std::string path;                   // full path from root, e.g. "sprites/player/idle.png"
    std::vector<entt::entity> children;
};

/**
 * @brief Hierarchical representation of the flat asset handle list.
 * All nodes are entities in an internal registry.
 * Directory nodes carry only vfs_node; file nodes additionally carry asset_handle.
 */
class vfs_tree {
public:
    vfs_tree()
    {
        _root = _reg.create();
        _reg.emplace<vfs_node>(_root);  // name="", path="", children={}
    }

    vfs_tree(const vfs_tree&) = delete;
    vfs_tree& operator=(const vfs_tree&) = delete;
    vfs_tree(vfs_tree&&) = default;
    vfs_tree& operator=(vfs_tree&&) = default;

    entt::entity root() const { return _root; }
    entt::registry& registry() { return _reg; }
    const entt::registry& registry() const { return _reg; }

private:
    entt::registry _reg;
    entt::entity _root { entt::null };
};

} // namespace nb
